/* ============================================================ */
/* h1_inference.h — Inférence MLP H+1 embarquée (C pur, sans NPU) */
/* Modèle : MLP 13→32(BN fusionné)→32→16→3  |  87.5% accuracy  */
/* Dépend de : h1_weights.h (généré par extract_weights.py)      */
/* ============================================================ */
#pragma once
#include <stdint.h>

/* ---- Classes ------------------------------------------------ */
typedef enum {
    H1_CLASS_CLAIR      = 0,
    H1_CLASS_PLUIE      = 1,
    H1_CLASS_BROUILLARD = 2,
    H1_CLASS_UNKNOWN    = -1
} H1Class;

/* ---- Résultat d'inférence ----------------------------------- */
typedef struct {
    H1Class label;          /* classe prédite                    */
    float   confidence;     /* score softmax de la classe gagnante (0..1) */
    float   scores[3];      /* scores softmax [Clair, Pluie, Brouillard] */
    int     ready;          /* 1 = assez d'historique pour calculer les deltas */
} H1Result;

/* ---- API ---------------------------------------------------- */

/**
 * Initialise le ring buffer et les variables internes.
 * À appeler une fois avant tout push.
 */
void h1_init(void);

/**
 * Pousse une nouvelle mesure capteur dans le ring buffer.
 * À appeler à chaque cycle de mesure (~20 s).
 *
 * @param temp_c   Température en °C
 * @param rhum_pct Humidité relative en %
 * @param pres_hpa Pression atmosphérique en hPa
 */
void h1_push(float temp_c, float rhum_pct, float pres_hpa);

/**
 * Renseigne l'heure et le mois courants pour les features temporelles.
 * Optionnel — par défaut : heure=12, mois=4 (valeurs neutres).
 * Appeler si un RTC ou NTP est disponible.
 *
 * @param hour  Heure locale (0–23)
 * @param month Mois (1–12)
 */
void h1_set_time(int hour, int month);

/**
 * Lance l'inférence sur la dernière mesure poussée.
 * Construit les 13 features, normalise, forward pass MLP.
 *
 * @return H1Result.ready = 0 si pas assez d'historique (< 3h de données),
 *                          1 sinon avec label + confidence remplis.
 */
H1Result h1_infer(void);

/**
 * Retourne le nom texte de la classe.
 */
const char *h1_class_name(H1Class c);
