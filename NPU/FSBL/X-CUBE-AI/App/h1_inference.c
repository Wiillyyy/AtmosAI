/* ============================================================ */
/* h1_inference.c — Inférence MLP H+1 embarquée (C pur, sans NPU) */
/* ============================================================ */
#include "h1_inference.h"
#include "h1_weights.h"       /* généré par extract_weights.py  */
#include "stm32n6xx_hal.h"    /* HAL_GetTick()                  */
#include <math.h>
#include <string.h>

/* ============================================================ */
/* Ring buffer                                                   */
/* ============================================================ */

/*
 * 560 entrées × ~20 s = ~3h7min.
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
static int      s_head  = 0;   /* prochain slot d'écriture           */
static int      s_count = 0;   /* nombre d'entrées valides (<= BUF)  */
static int      s_hour  = 12;  /* heure courante (0–23), défaut midi */
static int      s_month = 4;   /* mois courant  (1–12), défaut avril */

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
/* Accès au ring buffer — index k = 0 : plus récent             */
/* ============================================================ */
static const H1Sample *buf_at(int k)
{
    /* k=0 → dernière entrée écrite */
    int idx = (s_head - 1 - k + H1_BUF_LEN * 2) % H1_BUF_LEN;
    return &s_buf[idx];
}

/*
 * Retourne l'index (dans le sens k) de l'entrée dont le timestamp
 * est le plus proche de (tick_now - target_ms).
 * Retourne -1 si on n'a pas d'entrée aussi ancienne.
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

    /* Acceptable si on est dans les ±5 min de la cible */
    if (best_diff > 300000u) return -1;
    return best_k;
}

/* ============================================================ */
/* Fonctions mathématiques embarquées                           */
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
/* Forward pass : Dense(n_in → n_out, ReLU optionnel)           */
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
/* Point de rosée — formule de Magnus                           */
/* ============================================================ */
static float dewpoint(float temp_c, float rhum_pct)
{
    const float a = 17.625f, b = 243.04f;
    /* clamp rhum pour éviter log(0) */
    if (rhum_pct < 0.1f) rhum_pct = 0.1f;
    float alpha = logf(rhum_pct / 100.0f) + (a * temp_c) / (b + temp_c);
    return (b * alpha) / (a - alpha);
}

/* ============================================================ */
/* Inférence principale                                         */
/* ============================================================ */
H1Result h1_infer(void)
{
    H1Result result;
    memset(&result, 0, sizeof(result));
    result.label = H1_CLASS_UNKNOWN;
    result.ready = 0;

    if (s_count < 1) return result;

    /* ── Entrée courante ──────────────────────────────────── */
    const H1Sample *cur = buf_at(0);
    float temp = cur->temp;
    float rhum = cur->rhum;
    float pres = cur->pres;
    uint32_t now_tick = cur->tick_ms;

    /* ── Entrées passées ─────────────────────────────────── */
    /* 1h = 3 600 000 ms,  3h = 10 800 000 ms */
    int k1h = find_ago_k(now_tick, 3600000u);
    int k3h = find_ago_k(now_tick, 10800000u);

    float temp_d1h = 0.0f, temp_d3h = 0.0f;
    float pres_d1h = 0.0f, pres_d3h = 0.0f;
    float rhum_d1h = 0.0f;

    if (k1h >= 0) {
        const H1Sample *p1h = buf_at(k1h);
        temp_d1h = temp - p1h->temp;
        pres_d1h = pres - p1h->pres;
        rhum_d1h = rhum - p1h->rhum;
        result.ready = 1;
    }
    if (k3h >= 0) {
        const H1Sample *p3h = buf_at(k3h);
        temp_d3h = temp - p3h->temp;
        pres_d3h = pres - p3h->pres;
    }

    /* On infère même sans historique complet — deltas = 0 par défaut */
    result.ready = 1;

    /* ── Features temporelles ────────────────────────────── */
    float h     = (float)s_hour;
    float mo    = (float)s_month;
    float hsin  = sinf(2.0f * 3.14159265f * h  / 24.0f);
    float hcos  = cosf(2.0f * 3.14159265f * h  / 24.0f);
    float msin  = sinf(2.0f * 3.14159265f * mo / 12.0f);
    float mcos  = cosf(2.0f * 3.14159265f * mo / 12.0f);

    /* ── Point de rosée ──────────────────────────────────── */
    float dwpt_diff = temp - dewpoint(temp, rhum);

    /* ── Vecteur features [13] ───────────────────────────── */
    float x[13] = {
        temp,      rhum,      pres,
        temp_d1h,  temp_d3h,
        pres_d1h,  pres_d3h,
        rhum_d1h,
        hsin,      hcos,
        msin,      mcos,
        dwpt_diff
    };

    /* ── StandardScaler ──────────────────────────────────── */
    for (int i = 0; i < 13; i++)
        x[i] = (x[i] - H1_SCALER_MEAN[i]) / H1_SCALER_SCALE[i];

    /* ── Forward pass ────────────────────────────────────── */
    float h0[32], h1_[32], h2[16], out[3];

    /* Layer 0 : W0f (13×32) + b0f — BN déjà fusionné */
    dense(x, 13, (const float *)H1_W0, H1_B0, h0, 32, 1 /*ReLU*/);

    /* Layer 1 : W1 (32×32) */
    dense(h0, 32, (const float *)H1_W1, H1_B1, h1_, 32, 1 /*ReLU*/);

    /* Layer 2 : W2 (32×16) */
    dense(h1_, 32, (const float *)H1_W2, H1_B2, h2, 16, 1 /*ReLU*/);

    /* Layer 3 : W3 (16×3) + Softmax */
    dense(h2, 16, (const float *)H1_W3, H1_B3, out, 3, 0 /*no ReLU*/);
    h1_softmax(out, 3);

    /* ── Résultat ────────────────────────────────────────── */
    result.scores[0] = out[0];
    result.scores[1] = out[1];
    result.scores[2] = out[2];

    int best = 0;
    for (int i = 1; i < 3; i++)
        if (out[i] > out[best]) best = i;

    result.label      = (H1Class)best;
    result.confidence = out[best];
    result.ready      = 1;

    return result;
}

/* ============================================================ */
/* Construit + normalise les 13 features pour le NPU            */
/* (même calcul que h1_infer() mais sans le forward pass)       */
/* ============================================================ */
int h1_build_features(float *x13_out)
{
    if (s_count < 1) return 0;

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

    if (k1h >= 0) {
        const H1Sample *p1h = buf_at(k1h);
        temp_d1h = temp - p1h->temp;
        pres_d1h = pres - p1h->pres;
        rhum_d1h = rhum - p1h->rhum;
    }
    if (k3h >= 0) {
        const H1Sample *p3h = buf_at(k3h);
        temp_d3h = temp - p3h->temp;
        pres_d3h = pres - p3h->pres;
    }

    float h  = (float)s_hour;
    float mo = (float)s_month;
    float hsin = sinf(2.0f * 3.14159265f * h  / 24.0f);
    float hcos = cosf(2.0f * 3.14159265f * h  / 24.0f);
    float msin = sinf(2.0f * 3.14159265f * mo / 12.0f);
    float mcos = cosf(2.0f * 3.14159265f * mo / 12.0f);
    float dwpt_diff = temp - dewpoint(temp, rhum);

    float x[13] = {
        temp,      rhum,      pres,
        temp_d1h,  temp_d3h,
        pres_d1h,  pres_d3h,
        rhum_d1h,
        hsin,      hcos,
        msin,      mcos,
        dwpt_diff
    };

    for (int i = 0; i < 13; i++)
        x13_out[i] = (x[i] - H1_SCALER_MEAN[i]) / H1_SCALER_SCALE[i];

    return 1;
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
