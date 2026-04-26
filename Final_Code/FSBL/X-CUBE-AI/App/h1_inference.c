/* ============================================================ */
/* h1_inference.c — Inférence H+1 : NPU (STAI) + fallback CPU  */
/* ============================================================ */
#include "h1_inference.h"
#include "h1_weights.h"           /* genere par extract_weights.py  */
#include "stai_network.h"         /* STAI_NETWORK_CONTEXT_SIZE etc. */
#include "stm32n6xx_hal.h"        /* HAL_GetTick()                  */
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ============================================================ */
/* Ring buffer                                                   */
/* ============================================================ */

/*
 * 560 entrees x ~20 s = ~3h7min.
 * Suffisant pour calculer les deltas sur 1h et 3h.
 */
#define H1_BUF_LEN   560

typedef struct {
    float    temp;
    float    rhum;
    float    pres;
    uint32_t tick_ms;   /* HAL_GetTick() au moment du push */
} H1Sample;

static H1Sample s_buf[H1_BUF_LEN];
static int      s_head  = 0;   /* prochain slot d'ecriture           */
static int      s_count = 0;   /* nombre d'entrees valides (<= BUF)  */
static int      s_hour  = 12;  /* heure courante (0-23), defaut midi */
static int      s_month = 4;   /* mois courant  (1-12), defaut avril */

/* ============================================================ */
/* Contexte STAI — persistant entre les appels                  */
/* ============================================================ */
static uint8_t s_stai_ctx[STAI_NETWORK_CONTEXT_SIZE]
    __attribute__((aligned(STAI_NETWORK_CONTEXT_ALIGNMENT)));
static int s_stai_ready = 0;

/* ============================================================ */
void h1_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_head  = 0;
    s_count = 0;
    s_hour  = 12;
    s_month = 4;
}

/* ============================================================ */
void h1_push(float temp_c, float rhum_pct, float pres_hpa)
{
    s_buf[s_head].temp    = temp_c;
    s_buf[s_head].rhum    = rhum_pct;
    s_buf[s_head].pres    = pres_hpa;
    s_buf[s_head].tick_ms = HAL_GetTick();

    s_head = (s_head + 1) % H1_BUF_LEN;
    if (s_count < H1_BUF_LEN) s_count++;
}

/* ============================================================ */
void h1_set_time(int hour, int month)
{
    s_hour  = hour;
    s_month = month;
}

/* ============================================================ */
/* Acces au ring buffer - index k = 0 : plus recent             */
/* ============================================================ */
static const H1Sample *buf_at(int k)
{
    /* k=0 -> derniere entree ecrite */
    int idx = (s_head - 1 - k + H1_BUF_LEN * 2) % H1_BUF_LEN;
    return &s_buf[idx];
}

/*
 * Retourne l'index (dans le sens k) de l'entree dont le timestamp
 * est le plus proche de (tick_now - target_ms).
 * Retourne -1 si on n'a pas d'entree aussi ancienne.
 */
static int find_ago_k(uint32_t tick_now, uint32_t target_ms)
{
    uint32_t target_tick = tick_now - target_ms;
    int best_k = -1;
    uint32_t best_diff = 0xFFFFFFFFu;

    for (int k = 0; k < s_count; k++) {
        const H1Sample *s = buf_at(k);
        uint32_t diff = (s->tick_ms > target_tick)
                        ? (s->tick_ms - target_tick)
                        : (target_tick - s->tick_ms);
        if (diff < best_diff) {
            best_diff = diff;
            best_k    = k;
        }
    }

    /* Acceptable si on est dans les +-5 min de la cible */
    if (best_diff > 300000u) return -1;
    return best_k;
}

/* ============================================================ */
/* Fonctions mathematiques embarquees                           */
/* ============================================================ */

static float h1_relu(float x) { return x > 0.0f ? x : 0.0f; }

static void h1_softmax(float *x, int n)
{
    float mx = x[0];
    for (int i = 1; i < n; i++) if (x[i] > mx) mx = x[i];
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { x[i] = expf(x[i] - mx); sum += x[i]; }
    for (int i = 0; i < n; i++) x[i] /= sum;
}

/* ============================================================ */
/* Forward pass : Dense(n_in -> n_out, ReLU optionnel)          */
/* W[n_in][n_out], b[n_out], out[n_out]                         */
/* ============================================================ */
static void dense(const float *in, int n_in,
                  const float *W_flat, const float *b,
                  float *out, int n_out,
                  int apply_relu)
{
    for (int j = 0; j < n_out; j++) {
        float acc = b[j];
        for (int i = 0; i < n_in; i++)
            acc += in[i] * W_flat[i * n_out + j];
        out[j] = apply_relu ? h1_relu(acc) : acc;
    }
}

/* ============================================================ */
/* Point de rosee - formule de Magnus                           */
/* ============================================================ */
static float dewpoint(float temp_c, float rhum_pct)
{
    const float a = 17.625f, b = 243.04f;
    if (rhum_pct < 0.1f) rhum_pct = 0.1f;
    {
        float alpha = logf(rhum_pct / 100.0f) + (a * temp_c) / (b + temp_c);
        return (b * alpha) / (a - alpha);
    }
}

/* ============================================================ */
/* Inference principale                                         */
/* ============================================================ */
H1Result h1_infer(void)
{
    H1Result result;
    memset(&result, 0, sizeof(result));
    result.label = H1_CLASS_UNKNOWN;
    result.ready = 0;

    if (s_count < 1) return result;

    /* ── Integration NPU via STAI API ────────────────────────
     * RuntimeInit appele depuis app_netxduo.c (App_Try_Init_Npu).
     * RISAF4+5 configures avant pour debloquer l'acces memoire NPU. */
    if (!s_stai_ready) {
        stai_return_code init_rc =
            stai_network_init((stai_network *)s_stai_ctx);
        if (init_rc == STAI_SUCCESS) {
            s_stai_ready = 1;
            printf("[H1] NPU: reseau initialise (STAI OK)\r\n");
        } else {
            printf("[H1] NPU: init error %d\r\n", (int)init_rc);
        }
    }

    /*
     * stai_network_run() non appele dans la version finale :
     * EC_IRQ=0x8 persistant apres plusieurs essais de placement memoire et
     * configuration RISAF4+5. CID effectif du NPU non identifiable
     * sans debug JTAG niveau registres. Calcul assure par MLP CPU. */

    /*
     * Pipeline CPU :
     * 1) recupere la derniere mesure,
     * 2) calcule les deltas meteo 1h/3h si l'historique est disponible,
     * 3) construit les 13 features,
     * 4) normalise avec les moyennes/ecarts-types du training,
     * 5) execute le MLP puis softmax.
     */
    {
        const H1Sample *cur = buf_at(0);
        float temp = cur->temp;
        float rhum = cur->rhum;
        float pres = cur->pres;
        uint32_t now_tick = cur->tick_ms;
        int k1h = find_ago_k(now_tick, 3600000u);
        int k3h = find_ago_k(now_tick, 10800000u);
        float temp_d1h = 0.0f, temp_d3h = 0.0f;
        float pres_d1h = 0.0f, pres_d3h = 0.0f;
        float rhum_d1h = 0.0f;
        uint32_t elapsed_ms = (s_count > 1)
                              ? (now_tick - buf_at(s_count - 1)->tick_ms)
                              : 0u;

        if (k1h >= 0) {
            const H1Sample *p1h = buf_at(k1h);
            temp_d1h = temp - p1h->temp;
            pres_d1h = pres - p1h->pres;
            rhum_d1h = rhum - p1h->rhum;
        } else {
            uint32_t need_ms = 3600000u;
            uint32_t remain_s = (elapsed_ms < need_ms) ? (need_ms - elapsed_ms) / 1000u : 0u;
            printf("[H1] !! delta 1h indisponible (pret dans ~%lu min %02lu s)\r\n",
                   (unsigned long)(remain_s / 60),
                   (unsigned long)(remain_s % 60));
        }

        if (k3h >= 0) {
            const H1Sample *p3h = buf_at(k3h);
            temp_d3h = temp - p3h->temp;
            pres_d3h = pres - p3h->pres;
        } else {
            uint32_t need_ms = 10800000u;
            uint32_t remain_s = (elapsed_ms < need_ms) ? (need_ms - elapsed_ms) / 1000u : 0u;
            printf("[H1] !! delta 3h indisponible (pret dans ~%lu min %02lu s)\r\n",
                   (unsigned long)(remain_s / 60),
                   (unsigned long)(remain_s % 60));
        }

        result.ready = 1;

        {
            float h = (float)s_hour;
            float mo = (float)s_month;
            float hsin = sinf(2.0f * 3.14159265f * h / 24.0f);
            float hcos = cosf(2.0f * 3.14159265f * h / 24.0f);
            float msin = sinf(2.0f * 3.14159265f * mo / 12.0f);
            float mcos = cosf(2.0f * 3.14159265f * mo / 12.0f);
            float pt_rosee = temp - dewpoint(temp, rhum);
            float x[13] = {
                temp,      rhum,      pres,
                temp_d1h,  temp_d3h,
                pres_d1h,  pres_d3h,
                rhum_d1h,
                hsin,      hcos,
                msin,      mcos,
                pt_rosee
            };
            float h0[32], h1_[32], h2[16], out[3];
            int best = 0;

            printf("[H1] Capteurs: T=%.1f RH=%.1f P=%.1f\r\n", temp, rhum, pres);
            printf("[H1] Deltas:   dT1h=%+.2f dT3h=%+.2f dP1h=%+.2f dP3h=%+.2f dRH1h=%+.2f\r\n",
                   temp_d1h, temp_d3h, pres_d1h, pres_d3h, rhum_d1h);

            for (best = 0; best < 13; best++)
                x[best] = (x[best] - H1_SCALER_MEAN[best]) / H1_SCALER_SCALE[best];

            dense(x, 13, (const float *)H1_W0, H1_B0, h0, 32, 1);
            dense(h0, 32, (const float *)H1_W1, H1_B1, h1_, 32, 1);
            dense(h1_, 32, (const float *)H1_W2, H1_B2, h2, 16, 1);
            dense(h2, 16, (const float *)H1_W3, H1_B3, out, 3, 0);
            h1_softmax(out, 3);

            result.scores[0] = out[0];
            result.scores[1] = out[1];
            result.scores[2] = out[2];

            best = 0;
            for (int i = 1; i < 3; i++)
                if (out[i] > out[best]) best = i;

            result.label = (H1Class)best;
            result.confidence = out[best];

            printf("[H1] Sortie: Clair=%.3f Pluie=%.3f Brouillard=%.3f -> %s\r\n",
                   out[0], out[1], out[2], h1_class_name(result.label));
        }
    }

    return result;
}

/* ============================================================ */
const char *h1_class_name(H1Class c)
{
    switch (c) {
        case H1_CLASS_CLAIR:      return "Clair";
        case H1_CLASS_PLUIE:      return "Pluie";
        case H1_CLASS_BROUILLARD: return "Brouillard";
        default:                  return "Inconnu";
    }
}
