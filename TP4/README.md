# TP4 — Inférence IA embarquée sur STM32N657X0

> **ETRS606 — IA Embarquée · Université Savoie Mont Blanc**  
> William Z. · Franck G. · Mostapha K.

---

## Objectif

Déployer le modèle MLP entraîné en TP3 directement sur la carte STM32, sans cloud, sans NPU, et comparer les résultats avec les modèles cloud J+1/J+2/J+3 hébergés sur notre VPS.

---

## Ce qu'on a fait

### 1. Entraînement du modèle (Python / TensorFlow)

On a entraîné un MLP sur **43 821 heures** de données météo historiques (Open-Meteo, Aix-les-Bains 2019–2023) avec 3 classes : **Clair / Pluie / Neige**.

```
Architecture : Dense(32, ReLU) → Dense(32, ReLU) → Dense(16, ReLU) → Dense(3, Softmax)
Optimiseur   : Adam lr=1e-3
SMOTE        : rééquilibrage des classes
Accuracy     : 87.5 %
Taille       : ~8 Ko (poids C float32)
```

### 2. Conversion et déploiement via X-CUBE-AI

Le modèle `.keras` a été importé dans STM32CubeMX via le pack **X-CUBE-AI**, qui génère automatiquement le code C (`h1.c`, `h1_weights.h`).

On utilise `h1_infer()` — notre implémentation C float32 qui effectue le forward pass complet :

```c
h1_push(g_temperature, g_humidity, g_pressure);  // mise à jour ring buffer
H1Result res = h1_infer();                        // inférence < 1 ms
```

### 3. Les 13 features calculées en temps réel

Le modèle ne reçoit pas juste les 3 valeurs capteurs. Il reçoit **13 features** calculées depuis un **ring buffer de 560 échantillons** (~3h d'historique) maintenu en RAM :

| Feature | Description |
|---|---|
| temp, rhum, pres | Valeurs actuelles (capteurs directs) |
| ΔT 1h / ΔT 3h | Variation de température sur 1h et 3h |
| ΔP 1h / ΔP 3h | Variation de pression sur 1h et 3h |
| ΔH 1h | Variation d'humidité sur 1h |
| hour_sin / hour_cos | Heure encodée en cyclique (sin/cos) |
| month_sin / month_cos | Mois encodé en cyclique (sin/cos) |
| T − point de rosée | Formule de Magnus embarquée |

### 4. Résultat envoyé au VPS

En TP3 le JSON ne contenait que les mesures brutes. En TP4 il inclut la prédiction :

```json
{
  "device_id":     "NUCLEO-N657X0",
  "temperature":   22.4,
  "humidity":      58.1,
  "pressure":      1013.2,
  "prediction_h1": "Clair",
  "confidence_h1": 0.9231
}
```

### 5. Indicateur visuel sur les LEDs

| Prédiction | LED |
|---|---|
| ☀️ Clair | Verte allumée |
| 🌧️ Pluie | Rouge allumée |
| ❄️ Neige | Les deux allumées |

---

## Pourquoi le NPU n'est pas utilisé

La carte STM32N657X0 dispose d'un NPU intégré. On a d'abord tenté de l'utiliser via le runtime **LL_ATON** généré par X-CUBE-AI.

Au premier appel, **BusFault** — crash immédiat. Analyse du registre CFSR :

```
Adresse fautive : 0x342e0000  →  AXISRAM5 (npuRAM5)
Cause           : zone mémoire câblée exclusivement sur le bus AXI du NPU
                  le CPU (D-bus) n'a aucun chemin d'accès vers cette zone
```

De plus, X-CUBE-AI avait compilé tous les blocs en `EpochBlock_Flags_pure_sw` — le NPU matériel n'était de toute façon pas sollicité pour un modèle aussi petit (~8 Ko).

**Solution :** `h1_infer()`, implémentation C float32 pure, mêmes poids, même résultat, sans aucune dépendance mémoire problématique.

---

## Mesure de performance (DWT)

On mesure la durée d'inférence via le **compteur DWT** (Data Watchpoint and Trace) du Cortex-M55 — précis à la nanoseconde :

```
[H1] Prediction : Clair  (conf=92.3%)  inference=0.18 us
[PWR] CPU load  : 0.3 %
[PWR] P estimee : 100 mW
```

---

## Comparaison Edge vs Cloud

| Critère | STM32 H+1 (Edge) | VPS J+1/J+2/J+3 (Cloud) |
|---|---|---|
| Horizon | H+1 | J+1 / J+2 / J+3 |
| Accuracy | **87.5 %** | 64.3 % / 61.2 % / 54.4 % |
| Taille modèle | **~8 Ko** | ~120 Ko |
| Latence | **< 1 ms** | ~50 ms |
| Dépendance réseau | **Aucune** | Requiert connexion |
| Consommation | **~100 mW** | Serveur 24/7 |

Le modèle embarqué est plus précis sur l'horizon H+1 et totalement autonome. Les modèles cloud apportent de la valeur sur les horizons plus longs (J+1 à J+3) sans alourdir la carte.

---

## Fichiers

| Fichier | Rôle |
|---|---|
| `main.c` | Démarrage, init HAL, entrée ThreadX |
| `app_netxduo.c` | Threads capteurs + TCP, inférence H+1, envoi VPS |
| `h1_inference.c` | Ring buffer, calcul features, forward pass MLP |
| `h1_weights.h` | Poids du réseau (généré par `extract_weights.py`) |
