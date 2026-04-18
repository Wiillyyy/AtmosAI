# AtmosAI — Station Météo Embarquée avec IA Edge & Cloud

> **Module ETRS606 — IA Embarquée · Université Savoie Mont Blanc**  
> Développé dans le cadre du module ETS606_SPI

---

## Équipe

| Membre | Surnom dans l'équipe |
|---|---|
| **William Z.** | The Sovereign Lord of the TRI |
| **Franck G.** | The Stoic Sentinel of the CPU Debugger |
| **Mostapha K.** | The Holy Exorcist of the ESET UART Console |

---

## Table des matières

1. [Vue d'ensemble du système](#1-vue-densemble-du-système)
2. [Architecture globale](#2-architecture-globale)
3. [Matériel utilisé](#3-matériel-utilisé)
4. [Firmware STM32 — Azure RTOS](#4-firmware-stm32--azure-rtos)
5. [Modèle IA embarqué H+1](#5-modèle-ia-embarqué-h1)
6. [Pipeline ML — Entraînement Python/TensorFlow](#6-pipeline-ml--entraînement-pythontensorflow)
7. [Backend VPS — API Flask](#7-backend-vps--api-flask)
8. [Modèles Cloud J+1 / J+2 / J+3](#8-modèles-cloud-j1--j2--j3)
9. [Dashboard Web](#9-dashboard-web)
10. [Page d'administration](#10-page-dadministration)
11. [Référence API complète](#11-référence-api-complète)
12. [Résultats et performances](#12-résultats-et-performances)
13. [Comparaison Edge AI vs Cloud AI](#13-comparaison-edge-ai-vs-cloud-ai)
14. [Structure du projet](#14-structure-du-projet)
15. [Déploiement — Guide pas à pas](#15-déploiement--guide-pas-à-pas)
16. [Décisions techniques notables](#16-décisions-techniques-notables)
17. [Conformité aux critères ETRS606](#17-conformité-aux-critères-etrs606)

---

## 1. Vue d'ensemble du système

**AtmosAI** est un système de classification météorologique embarqué bout-en-bout :

- Une carte **STM32 NUCLEO-N657X0** acquiert en continu la température, l'humidité et la pression via des capteurs MEMS.
- Un **réseau de neurones MLP embarqué** tourne en temps réel sur le microcontrôleur et prédit la météo à **H+1** (dans 1 heure).
- Les mesures et prédictions sont envoyées par **TCP/IP HTTP POST** à un **serveur VPS** hébergeant une **API Flask**.
- Le VPS stocke les données dans **SQLite** et lance en parallèle trois autres modèles Keras pour prédire à **J+1, J+2, J+3**.
- Un **dashboard web** accessible publiquement affiche les données en temps réel, l'historique, les graphiques et les prédictions.

Le projet remplace volontairement la stack ThingSpeak/MATLAB imposée par le sujet TP3/TP4 par une solution **auto-hébergée** Flask, validée et encouragée par l'encadrant.

---

## 2. Architecture globale

```
┌─────────────────────────────────────────────────────────────────┐
│                     CARTE STM32N657X0                           │
│                                                                 │
│  ┌─────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  HTS221     │    │  LPS22HH     │    │  Azure RTOS      │   │
│  │  Temp/Rhum  │───▶│  Pression    │───▶│  ThreadX         │   │
│  └─────────────┘    └──────────────┘    │  2 threads :     │   │
│                                         │  • Sensor Thread │   │
│  ┌──────────────────────────────────┐   │  • TCP Thread    │   │
│  │  MLP H+1 — h1_infer()           │   └──────────────────┘   │
│  │  C float32, ~8 Ko               │                           │
│  │  13 features → 3 classes        │                           │
│  │  Clair / Pluie / Neige          │                           │
│  │  87.5 % accuracy                │                           │
│  └──────────────────────────────────┘                           │
│                                                                 │
│  ┌──────────────────────────────────┐                           │
│  │  NetXDuo — TCP/IP Stack          │                           │
│  │  HTTP POST toutes les ~20s       │                           │
│  └──────────────────────────────────┘                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │ Ethernet / HTTP POST
                          │ JSON : temp, rhum, pres,
                          │        prediction_h1, confidence_h1
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      VPS — Flask API                            │
│                                                                 │
│  POST /api/data ──▶ SQLite (meteo.db)                          │
│                          │                                      │
│                          ▼                                      │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐                   │
│  │ Keras J+1 │  │ Keras J+2 │  │ Keras J+3 │                   │
│  │ 64.3% bal │  │ 61.2% bal │  │ 54.4% bal │                   │
│  └───────────┘  └───────────┘  └───────────┘                   │
│                                                                 │
│  GET /api/data  GET /api/forecast  GET /api/stats               │
│  GET /health    GET /api/command   POST /api/command            │
└─────────────────────────┬───────────────────────────────────────┘
                          │ HTTP GET (polling ~15s)
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Dashboard Web                                │
│                                                                 │
│  index.html — Chart.js + Canvas API                            │
│  • Valeurs temps réel  (Temp / Humidité / Pression)            │
│  • Sparklines 3 capteurs                                       │
│  • Prédiction H+1 STM32 (classe + confiance + barre)           │
│  • Tableau historique paginé                                    │
│  • Graphiques Chart.js (Historique)                            │
│  • Page Modèle IA (architecture, features, comparaison)        │
│  • admin.html (gestion DB, purge, commandes)                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Matériel utilisé

### Carte principale
| Composant | Détail |
|---|---|
| Microcontrôleur | **STM32N657X0** (Arm Cortex-M55, 800 MHz, 4 MB Flash, 4 MB RAM) |
| Carte de développement | **NUCLEO-N657X0** |
| OS temps réel | **Azure RTOS ThreadX** |
| Stack réseau | **NetXDuo** (middleware ST) |
| Connectivité | Ethernet RJ45 via PHY embarqué |
| NPU | **Présent mais non utilisé** (voir §16) |

### Carte fille capteurs
| Composant | Détail |
|---|---|
| Shield | **X-NUCLEO-IKS01A3** (ST MEMS sensor expansion) |
| Capteur température/humidité | **HTS221** — précision ±0.5°C / ±3.5% RH |
| Capteur pression | **LPS22HH** — précision ±0.1 hPa |
| Interface | I2C |

### Serveur
| Élément | Détail |
|---|---|
| Hébergement | VPS Linux (Debian) |
| Serveur applicatif | **Gunicorn** (production) / Flask dev server (test) |
| Base de données | **SQLite** (fichier `meteo.db`) |
| Déploiement | systemd service, démarrage automatique |

---

## 4. Firmware STM32 — Azure RTOS

### Organisation des threads

Le firmware repose sur **Azure RTOS ThreadX** avec deux threads applicatifs :

```
ThreadX Kernel
├── App Sensor Thread  (priorité NX_APP_THREAD_PRIORITY + 1)
│   ├── Lecture HTS221 → température, humidité
│   ├── Lecture LPS22HH → pression
│   ├── Calcul des 13 features (deltas, cycliques, point de rosée)
│   ├── Appel h1_infer() → classe météo H+1 + confiance
│   └── tx_thread_sleep(200)  → cycle ~20s
│
└── App TCP Thread     (priorité NX_APP_THREAD_PRIORITY)
    ├── Connexion TCP au VPS
    ├── Construction JSON payload
    ├── HTTP POST /api/data
    └── Lecture réponse + tx_thread_sleep(200)
```

> **Note sur la synchronisation** : Les deux threads tournent sur des cycles indépendants de ~20 secondes. Il existe donc une légère désynchronisation temporelle entre ce qu'affiche la console UART (valeur mesurée par le thread capteur) et ce que reçoit le VPS (valeur postée par le thread TCP lors de son prochain cycle). Ce comportement est attendu et inhérent à l'architecture multi-thread asynchrone.

### Allocation mémoire

```c
// Stack statique — contourne le byte pool ThreadX, évite tout débordement
static UCHAR s_sensor_stack[4096];

// Création du thread sans allocation dynamique
tx_thread_create(
    &AppSensorThread, "App Sensor Thread",
    App_Sensor_Thread_Entry, 0,
    s_sensor_stack, sizeof(s_sensor_stack),
    NX_APP_THREAD_PRIORITY + 1, NX_APP_THREAD_PRIORITY + 1,
    TX_NO_TIME_SLICE, TX_AUTO_START
);
```

### Bannière UART au démarrage

```
  ___  _                      _   ___ 
 / _ \| |_ _ __  _____   __ _| | |_ _|
| |_| | __| '_ \/ _ \ \ / _` | |  | | 
|  _  | |_| | | | (_) | | (_| | |  | | 
|_| |_|\__|_| |_|\___/ \_\__,_|_| |___|

  Station meteo embarquee — ETRS 606
  STM32N657X0  |  Azure RTOS  |  MLP H+1

  Equipe :
    William Z.
    Franck G.
    Mostapha K.

  ==========================================
```

### Payload JSON envoyé par la carte

```json
{
  "device_id":      "nucleo",
  "temperature":    22.4,
  "humidity":       58.1,
  "pressure":       1013.2,
  "prediction_h1":  "Clair",
  "confidence_h1":  0.9231
}
```

---

## 5. Modèle IA embarqué H+1

### Principe

Le modèle tourne **entièrement sur le STM32**, sans aucune dépendance réseau. Il prédit la classe météo à **H+1** (dans 1 heure) à partir des capteurs locaux.

### Architecture MLP

```
Input (13 features)
        │
        ▼
  Dense(32, ReLU)
        │
        ▼
  Dense(32, ReLU)
        │
        ▼
  Dense(16, ReLU)
        │
        ▼
  Dense(3, Softmax)
        │
        ▼
  [Clair, Pluie, Neige]
```

### Les 13 features d'entrée

| # | Feature | Source | Description |
|---|---|---|---|
| 1 | `temp` | HTS221 | Température actuelle (°C) |
| 2 | `rhum` | HTS221 | Humidité relative actuelle (%) |
| 3 | `pres` | LPS22HH | Pression actuelle (hPa) |
| 4 | `temp_delta_1h` | Ring buffer | Variation de température sur 1h |
| 5 | `temp_delta_3h` | Ring buffer | Variation de température sur 3h |
| 6 | `pres_delta_1h` | Ring buffer | Variation de pression sur 1h |
| 7 | `pres_delta_3h` | Ring buffer | Variation de pression sur 3h |
| 8 | `rhum_delta_1h` | Ring buffer | Variation d'humidité sur 1h |
| 9 | `hour_sin` | RTC | sin(2π × heure / 24) — cyclique |
| 10 | `hour_cos` | RTC | cos(2π × heure / 24) — cyclique |
| 11 | `month_sin` | RTC | sin(2π × mois / 12) — cyclique |
| 12 | `month_cos` | RTC | cos(2π × mois / 12) — cyclique |
| 13 | `temp_dwpt_diff` | Calculé | Température − point de rosée (formule Magnus) |

> **Toutes ces features sont calculables en temps réel** depuis les capteurs embarqués, sans aucune donnée externe. Le ring buffer de 560 échantillons (~3h à 20s/mesure) est maintenu en RAM.

### Point de rosée — Formule Magnus (C embarqué)

```c
// Paramètres Magnus
#define MAGNUS_A 17.625f
#define MAGNUS_B 243.04f

float alpha = logf(rhum / 100.0f) + (MAGNUS_A * temp) / (MAGNUS_B + temp);
float dwpt  = (MAGNUS_B * alpha) / (MAGNUS_A - alpha);
features[12] = temp - dwpt;
```

### Conversion et déploiement

```
Python/Keras model (.keras)
         │
         ▼ X-CUBE-AI (STM32CubeMX)
         │
    model.c / model.h  +  h1.c / h1.h
         │
         ▼
    h1_infer()  ←  appelé dans App_Sensor_Thread_Entry
```

> **Pourquoi `h1_infer()` et pas l'API ATON ?**  
> Le modèle compilé par X-CUBE-AI génère des `EpochBlock_Flags_pure_sw` — tous les blocs sont exécutés en logiciel pur, sans NPU matériel. De plus, les buffers d'entrée/sortie ATON pointent vers `0x342e0000` (AXISRAM5 / npuRAM5), un segment SRAM exclusivement accessible par le bus AXI du NPU, **inaccessible au CPU** (BusFault garanti). `h1_infer()` est une implémentation C float32 mathématiquement identique — mêmes poids, même calcul — sans aucune dépendance mémoire problématique. Voir §16 pour l'analyse complète.

---

## 6. Pipeline ML | Entraînement Python/TensorFlow

### Source de données

| Paramètre | Valeur |
|---|---|
| Bibliothèque | `meteostat` + `open-meteo` (données historiques libres) |
| Localisation | **Aix-les-Bains** — 45.69°N 5.92°E — 250 m |
| Période | 2019 – 2023 |
| Nombre d'observations | ~43 821 |
| Fréquence | Horaire |

### Classes météo retenues

| Classe | Justification |
|---|---|
| ☀️ **Clair** | Classe majoritaire, bien représentée dans les données Aix-les-Bains |
| 🌧️ **Pluie** | Précipitations fréquentes en Savoie |
| ❄️ **Neige** | Événements hivernaux significatifs à l'altitude de la station |

> Choix de **3 classes** après étude du compromis : au-delà de 3 classes, les données historiques Open-Meteo ne contiennent pas assez d'événements de Brouillard, Orage ou Vent fort pour entraîner un modèle robuste sur cette localisation. La balanced accuracy chutait à <45% au-delà de 5 classes.

### Pipeline d'entraînement

```python
# 1. Chargement et nettoyage
df = fetch_open_meteo(lat=45.69, lon=5.92, start="2019-01-01", end="2023-12-31")
df = df.dropna(subset=["temperature_2m", "relative_humidity_2m", "pressure_msl"])

# 2. Feature engineering (identique aux features embarquées)
df["temp_delta_1h"] = df["temperature_2m"].diff(1)
df["temp_delta_3h"] = df["temperature_2m"].diff(3)
df["pres_delta_1h"] = df["pressure_msl"].diff(1)
df["pres_delta_3h"] = df["pressure_msl"].diff(3)
df["rhum_delta_1h"] = df["relative_humidity_2m"].diff(1)
df["hour_sin"]      = np.sin(2 * np.pi * df.index.hour / 24)
df["hour_cos"]      = np.cos(2 * np.pi * df.index.hour / 24)
df["month_sin"]     = np.sin(2 * np.pi * df.index.month / 12)
df["month_cos"]     = np.cos(2 * np.pi * df.index.month / 12)
# Formule Magnus pour temp_dwpt_diff
alpha = np.log(df["relative_humidity_2m"] / 100) + \
        (17.625 * df["temperature_2m"]) / (243.04 + df["temperature_2m"])
df["dwpt"]          = (243.04 * alpha) / (17.625 - alpha)
df["temp_dwpt_diff"]= df["temperature_2m"] - df["dwpt"]

# 3. Rééquilibrage SMOTE
from imblearn.over_sampling import SMOTE
X_res, y_res = SMOTE(random_state=42).fit_resample(X_train, y_train)

# 4. Normalisation
from sklearn.preprocessing import StandardScaler
scaler = StandardScaler()
X_res  = scaler.fit_transform(X_res)

# 5. Architecture MLP
model = tf.keras.Sequential([
    tf.keras.layers.Dense(32, activation='relu', input_shape=(13,)),
    tf.keras.layers.Dense(32, activation='relu'),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(3,  activation='softmax'),
])
model.compile(optimizer=Adam(lr=1e-3), loss='sparse_categorical_crossentropy',
              metrics=['accuracy'])

# 6. Entraînement
model.fit(X_res, y_res, epochs=100, batch_size=32,
          validation_split=0.2,
          callbacks=[EarlyStopping(patience=10, restore_best_weights=True)])

# 7. Export pour X-CUBE-AI
model.save("meteo_h1_model.keras")
```

### Hyperparamètres retenus

| Hyperparamètre | Valeur | Justification |
|---|---|---|
| Couches cachées | 32 → 32 → 16 | Compromis taille/accuracy (<10 Ko en C) |
| Activation | ReLU | Standard, converti nativement par X-CUBE-AI |
| Optimiseur | Adam lr=1e-3 | Convergence rapide sur dataset tabulaire |
| Batch size | 32 | Bon équilibre vitesse/généralisation |
| Split | 80% train / 20% test | Standard ML |
| SMOTE | Oui | Classes très déséquilibrées (Neige <<Pluie < Clair) |
| Early stopping | patience=10 | Évite l'overfitting |

---

## 7. Backend VPS | API Flask

### Stack technique

```
Gunicorn (WSGI production)
    └── Flask (app.py)
            ├── SQLite (meteo.db) via database.py
            ├── TensorFlow/Keras (modèles J+1/J+2/J+3)
            ├── joblib (scalers .pkl)
            └── systemd (auto-restart)
```

### Base de données SQLite

```sql
CREATE TABLE sensor_data (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp      DATETIME DEFAULT CURRENT_TIMESTAMP,
    device_id      TEXT,
    temperature    REAL,
    humidity       REAL,
    pressure       REAL,
    wind_speed     REAL,     -- réservé, NULL pour l'instant
    wind_dir       REAL,     -- réservé, NULL pour l'instant
    prediction_h1  TEXT,     -- classe H+1 calculée par le STM32
    confidence_h1  REAL      -- confiance [0.0 – 1.0]
);
```

> **Chemin absolu** : la connexion utilise `os.path.dirname(os.path.abspath(__file__))` pour que Gunicorn trouve `meteo.db` quel que soit le répertoire de démarrage du service.

### Chargement des modèles au démarrage

```python
def load_forecast_models():
    for name in ("j1", "j2", "j3"):
        model  = tf.keras.models.load_model(f"model_{name}/meteo_{name}_model.keras")
        scaler = joblib.load(f"model_{name}/scaler_{name}.pkl")
        meta   = json.load(open(f"model_{name}/meta_{name}.json"))
        FORECAST[name] = {"model": model, "scaler": scaler, "meta": meta}
```

### Système de commande one-shot (downlink VPS → STM32)

Le VPS expose un mécanisme de commande descendante pour piloter la carte :

```
Admin POST /api/command  {"cmd": "dance"}
         │
         ▼  (stocké en mémoire)
STM32 GET /api/command  → {"cmd": "dance"}  ← consommé (reset → "none")
```

---

## 8. Modèles Cloud J+1 / J+2 / J+3

### Architecture

3 modèles MLP **indépendants** entraînés sur le même dataset mais avec des labels décalés dans le temps :

```
Architecture commune : 64 → 64(BN) → 32 → 3 (Softmax)
Optimiseur           : Adam lr=1e-3
Régularisation       : Dropout 0.3 + Batch Normalization
```

| Modèle | Horizon | Balanced Accuracy |
|---|---|---|
| J+1 | +24h | **64.3 %** |
| J+2 | +48h | **61.2 %** |
| J+3 | +72h | **54.4 %** |

> La dégradation de performance avec l'horizon est attendue : prédire à 72h avec uniquement des capteurs locaux (sans données NWP) est fondamentalement difficile.

### Features identiques au modèle embarqué

Les 13 features sont calculées **côté VPS** de manière identique à la carte (même formule Magnus, mêmes deltas), en interrogeant l'historique SQLite :

```python
ago1 = get_reading_ago(1)   # mesure il y a 1h
ago3 = get_reading_ago(3)   # mesure il y a 3h
temp_d1h = temp - ago1["temperature"]
# ... etc
```

### Déclenchement

L'inférence J+1/J+2/J+3 est disponible à la demande via `GET /api/forecast`. Le dashboard l'interroge à chaque cycle de rafraîchissement.

---

## 9. Dashboard Web

### Fichier : `atmosai/index.html`

Dashboard single-page en HTML/CSS/JS pur, sans framework. Thème sombre/clair commutable.

### Sections

#### Onglet Dashboard
- **3 cartes métriques temps réel** : Température (cyan), Humidité (amber), Pression (vert)
- **Carte Prédiction H+1** : emoji animé flottant, nom de la classe, barre de confiance, temp/pression prédites
- **Tableau historique** : 50 dernières mesures, device_id, classe H+1, confiance
- **3 sparklines** : mini-graphes Canvas 2D bézier pour temp/humidité/pression
- **Statistiques** : total mesures, moyennes, première/dernière mesure

#### Onglet Historique
- 3 graphiques **Chart.js** (ligne) pour température, humidité, pression
- Sélecteur du nombre de mesures (50 / 100 / 200 / 500)

#### Onglet Modèle IA
- Description du système AtmosAI
- Architecture MLP (diagramme textuel)
- Classes et recall
- Features (13 pills surlignées)
- Données d'entraînement + Déploiement STM32 (carte fusionnée)
- Modèles VPS J+1/J+2/J+3
- **Tableau comparatif Edge vs Cloud** (8 critères)

### Sparklines | implémentation Canvas 2D

```javascript
function drawSparkline(vals, canvasId, hexColor) {
  const canvas = document.getElementById(canvasId);
  const H = 160;
  canvas.style.height = H + 'px';
  canvas.width  = canvas.offsetWidth * dpr;
  canvas.height = H * dpr;
  // Courbe bézier + gradient de remplissage
  // ...
}

function drawSparklines(data) {
  drawSparkline(data.map(r=>r.temperature), 'sparkline-temp', '#00c8f0');
  drawSparkline(data.map(r=>r.humidity),    'sparkline-hum',  '#f5a623');
  drawSparkline(data.map(r=>r.pressure),    'sparkline-pres', '#3ddc84');
}
```

### Rafraîchissement automatique

Le dashboard poll le VPS toutes les **15 secondes** via `fetch('/api/data?n=50')`. Un compteur de progression visible informe l'utilisateur du prochain rechargement.

---

## 10. Page d'administration

### Fichier : `atmosai/admin.html`

Page protégée par clé admin (`X-Admin-Key: atmosai2026`), accessible uniquement en connaissant l'URL.

### Fonctionnalités

| Fonctionnalité | Endpoint | Description |
|---|---|---|
| Voir les devices | `GET /api/admin/devices` | Liste les devices avec count et dernière connexion |
| Purger > 30 jours | `POST /api/admin/purge` | Supprime les mesures anciennes |
| Vider la base | `POST /api/admin/purge-all` | Supprime **toutes** les mesures (reset démo) |
| Envoyer commande | `POST /api/command` | Commande one-shot vers la carte |
| Voir les stats | `GET /api/stats` | Statistiques globales de la DB |

---

## 11. Référence API complète

### Authentification

| Route | Auth requise | Type |
|---|---|---|
| `POST /api/data` | ✅ `X-API-Key` | Clé API device |
| `GET /api/data` | ❌ | Public |
| `GET /api/forecast` | ❌ | Public |
| `GET /api/stats` | ❌ | Public |
| `GET /health` | ❌ | Public |
| `POST /api/admin/purge` | ✅ `X-Admin-Key` | Clé admin |
| `POST /api/admin/purge-all` | ✅ `X-Admin-Key` | Clé admin |
| `GET /api/admin/devices` | ✅ `X-Admin-Key` | Clé admin |
| `GET /api/command` | ❌ | Public (one-shot) |
| `POST /api/command` | ✅ `X-Admin-Key` | Clé admin |

---

### `POST /api/data`

Envoie une mesure depuis la carte STM32.

**Headers :**
```
Content-Type: application/json
X-API-Key: <METEO_API_KEY>
```

**Body :**
```json
{
  "device_id":      "nucleo",
  "temperature":    22.4,
  "humidity":       58.1,
  "pressure":       1013.2,
  "prediction_h1":  "Clair",
  "confidence_h1":  0.9231
}
```

**Réponse 201 :**
```json
{
  "status": "ok",
  "timestamp": "2026-04-16T10:30:00.000000",
  "received": { "temperature": 22.4, "humidity": 58.1, "pressure": 1013.2 },
  "prediction_h1": "Clair",
  "confidence_h1": 0.9231
}
```

---

### `GET /api/data?n=50`

Récupère les N dernières mesures (max 1000).

**Réponse 200 :**
```json
{
  "count": 50,
  "data": [
    {
      "id": 1423,
      "timestamp": "2026-04-16T10:30:00",
      "device_id": "nucleo",
      "temperature": 22.4,
      "humidity": 58.1,
      "pressure": 1013.2,
      "prediction_h1": "Clair",
      "confidence_h1": 0.9231
    }
  ]
}
```

---

### `GET /api/forecast`

Lance l'inférence J+1/J+2/J+3 sur la dernière mesure en base.

**Réponse 200 :**
```json
{
  "based_on": {
    "timestamp": "2026-04-16T10:30:00",
    "temperature": 22.4,
    "humidity": 58.1,
    "pressure": 1013.2
  },
  "forecast": {
    "j1": { "label": "Clair", "confidence": 0.7823, "scores": {"Clair": 0.7823, "Pluie": 0.1654, "Neige": 0.0523} },
    "j2": { "label": "Pluie", "confidence": 0.6102, "scores": { ... } },
    "j3": { "label": "Pluie", "confidence": 0.5441, "scores": { ... } }
  }
}
```

---

### `GET /api/stats`

Statistiques globales de la base de données.

**Réponse 200 :**
```json
{
  "total": 1423,
  "temp_avg": 18.4,
  "hum_avg": 62.3,
  "pres_avg": 1011.8,
  "first": "2026-01-15T08:00:00",
  "last": "2026-04-16T10:30:00"
}
```

---

## 12. Résultats et performances

### Modèle H+1 embarqué (STM32)

| Métrique | Valeur |
|---|---|
| Accuracy globale | **87.5 %** |
| Balanced accuracy | ~85 % |
| Taille modèle (poids C float32) | **~8 Ko** |
| Temps d'inférence STM32N657X0 | < 1 ms (estimé, CPU 800 MHz) |
| Mémoire RAM modèle | < 4 Ko |
| Couches | 4 Dense (13→32→32→16→3) |
| Paramètres entraînables | ~2 800 |

### Modèles VPS (cloud)

| Modèle | Horizon | Balanced Accuracy | Architecture |
|---|---|---|---|
| J+1 | 24h | **64.3 %** | 64→64(BN)→32→3 |
| J+2 | 48h | **61.2 %** | 64→64(BN)→32→3 |
| J+3 | 72h | **54.4 %** | 64→64(BN)→32→3 |

### Recall par classe (modèle H+1)

| Classe | Recall |
|---|---|
| ☀️ Clair | ~92 % |
| 🌧️ Pluie | ~84 % |
| ❄️ Neige | ~81 % |

> La classe Neige a le recall le plus faible malgré SMOTE, car les événements de neige à 250m sont plus rares et plus bruités dans les données historiques.

---

## 13. Comparaison Edge AI vs Cloud AI

| Critère | STM32 H+1 (Edge) | VPS J+1/J+2/J+3 (Cloud) |
|---|---|---|
| **Horizon de prédiction** | H+1 | J+1 / J+2 / J+3 |
| **Architecture** | MLP 13→32→32→16→3 | MLP 13→128→128→64→32→3 |
| **Accuracy** | 87.5 % | 80.9 % (J+1 moy.) |
| **Taille modèle** | ~8 Ko | ~120 Ko (Keras) |
| **Latence inférence** | < 1 ms (local) | ~50 ms (réseau + GPU) |
| **Dépendance réseau** | ❌ Aucune | ✅ Requiert connexion |
| **Consommation énergie** | Très faible (MCU seul) | Élevée (serveur 24/7) |
| **Mise à jour modèle** | Reflashing firmware | Redémarrage Flask |
| **Scalabilité** | Non (1 device) | Oui (N devices) |
| **Bande passante** | Très faible (~200 B/req) | Idem (même payload) |

### Bilan architecture

L'approche **Edge-first** adoptée dans AtmosAI est cohérente avec les principes de développement soutenable (critère P_TEDS) :
- L'inférence critique (H+1) se fait **localement**, sans aucun round-trip réseau
- Le réseau n'est utilisé que pour la **collecte et l'archivage** des données
- Les modèles cloud J+1/J+2/J+3 apportent une valeur ajoutée (horizon plus long) sans bloquer le fonctionnement local
- Le modèle embarqué est **~15× plus petit** que les modèles cloud tout en étant plus précis sur l'horizon H+1

---

## 14. Structure du projet

```
AtmosAI_ETRS606/
├── FSBL/
│   ├── NetXDuo/
│   │   └── App/
│   │       └── app_netxduo.c          ← Logique principale : threads, TCP, inférence
│   └── X-CUBE-AI/
│       └── App/
│           ├── h1.c / h1.h            ← Modèle MLP généré par X-CUBE-AI
│           └── h1_inference.c         ← h1_infer() : inférence MLP pure C
│
atmosai/
├── index.html                         ← Dashboard web (single-page)
├── admin.html                         ← Page d'administration
└── .vscode/
    └── sftp.json                      ← Config déploiement SFTP → VPS
│
app.py                                 ← API Flask principale
database.py                            ← Accès SQLite (init, insert, get)
meteo.db                               ← Base de données SQLite
│
model_j1/
│   ├── meteo_j1_model.keras
│   ├── scaler_j1.pkl
│   └── meta_j1.json
model_j2/
│   ├── meteo_j2_model.keras
│   ├── scaler_j2.pkl
│   └── meta_j2.json
model_j3/
│   ├── meteo_j3_model.keras
│   ├── scaler_j3.pkl
│   └── meta_j3.json
│
train/                                 ← Scripts d'entraînement Python
│   ├── train_h1.py                    ← Entraînement modèle embarqué
│   ├── train_jn.py                    ← Entraînement modèles J+1/J+2/J+3
│   ├── extract_weights.py             ← Export poids → C float32
│   └── evaluate.py                    ← Métriques, matrices de confusion
│
README.md
```

---

## 15. Déploiement : Guide pas à pas

### A. Firmware STM32

1. Ouvrir `AtmosAI_ETRS606` dans **STM32CubeIDE**
2. Vérifier la configuration dans `app_netxduo.c` :
   - Adresse IP du VPS : `SERVER_IP`
   - Port : `SERVER_PORT` (5000 ou 80 si Nginx)
   - Clé API : `API_KEY`
3. Build → Flash sur la carte NUCLEO-N657X0
4. Observer la bannière dans le terminal UART (115200 bauds)

### B. Backend VPS

```bash
# Cloner le repo
git clone https://github.com/<votre-equipe>/atmosai.git
cd atmosai

# Installer les dépendances
pip install flask tensorflow joblib scikit-learn numpy

# Variables d'environnement
export METEO_API_KEY="votre_cle_api_secrete"

# Test local
python app.py

# Production avec Gunicorn
gunicorn -w 2 -b 0.0.0.0:5000 app:app
```

**Service systemd (`/etc/systemd/system/atmosai.service`) :**
```ini
[Unit]
Description=AtmosAI Flask API
After=network.target

[Service]
User=www-data
WorkingDirectory=/home/atmosai
Environment="METEO_API_KEY=votre_cle_api_secrete"
ExecStart=/usr/bin/gunicorn -w 2 -b 0.0.0.0:5000 app:app
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
systemctl enable atmosai
systemctl start atmosai
```

### C. Dashboard Web

Le dashboard est un **fichier HTML statique**. Il peut être servi directement par Nginx, Apache, ou tout CDN. Modifier l'URL de l'API dans `index.html` :

```javascript
const API_BASE = "https://votre-vps.com";  // ou "" si même domaine
```

### D. Entraîner / ré-entraîner les modèles

```bash
cd train/

# Modèle embarqué H+1
python train_h1.py
# → génère meteo_h1_model.keras + scaler_h1.pkl

# Convertir pour STM32 via X-CUBE-AI
# (dans STM32CubeMX : onglet X-CUBE-AI → importer meteo_h1_model.keras)

# Modèles cloud J+1/J+2/J+3
python train_jn.py
# → génère model_j1/, model_j2/, model_j3/

# Déployer sur VPS
rsync -avz model_j*/ user@vps:/home/atmosai/
systemctl restart atmosai
```

---

## 16. Décisions techniques notables

### Remplacement ThingSpeak → Flask auto-hébergé

**Raison :** ThingSpeak impose des limitations importantes en prototype :
- Rate limit : **1 POST toutes les 15 secondes minimum**
- Pas de contrôle sur le schéma de données
- Dépendance à une licence MathWorks pour MATLAB
- Impossible de déployer des modèles Keras nativement

**Solution retenue :** API Flask sur VPS Linux (Gunicorn + systemd), SQLite pour le stockage. Cette approche est **plus professionnelle, plus flexible et sans rate limit**.

### Abandon du NPU Analyse technique complète

Lors de l'intégration de X-CUBE-AI, le runtime LL_ATON provoquait un **BusFault** (Hard Fault) au premier appel `memcpy` vers l'adresse `0x342e0000`.

**Diagnostic :**
```
Analyse du registre CFSR (Configurable Fault Status Register) :
→ PRECISERR = 1 : faute de données précise
→ Adresse fautive : 0x342e0000
→ Mapping : AXISRAM5 / npuRAM5
```

**Cause racine :**
```
Carte mémoire STM32N657X0 :
  0x342e0000 → AXISRAM5 (npuRAM5)
  Connecté exclusivement au bus AXI du NPU
  Le CPU (D-bus) n'a AUCUN chemin d'accès vers cette zone
  → Toute tentative d'accès CPU = BusFault immédiat
```

**Aggravant :** L'analyse du fichier `h1.c` généré par X-CUBE-AI révèle que **tous** les blocs d'exécution sont flagués `EpochBlock_Flags_pure_sw` le NPU matériel n'est donc **pas utilisé du tout** même par le runtime ATON. Le modèle trop petit (< 10 Ko) ne justifie pas l'accélération NPU.

**Solution :** Utilisation directe de `h1_infer()`, une implémentation C float32 des mêmes poids, mathématiquement strictement identique, qui opère uniquement sur la SRAM accessible au CPU.

### Stack statique pour le thread capteur

Allocation **statique** de 4096 octets pour le stack du thread capteur, au lieu de l'allocation depuis le byte pool ThreadX. Cela évite tout risque de fragmentation ou de dépassement du byte pool, particulièrement important dans un contexte de démonstration continue.

---

## 17. Conformité aux critères ETRS606

### TP5 — Grille d'évaluation

| Critère | Code | Statut | Justification |
|---|---|---|---|
| Bonnes pratiques déploiement | **TR_IMPLEMENT** | ✅ | API REST propre, auth par clé, SQLite chemin absolu, Gunicorn+systemd, séparation routes |
| IA embarquée STM32 | **E_IMPLEMENT** | ✅ | MLP C float32 sur NUCLEO-N657X0, 13 features temps réel, 87.5% accuracy, démonstration live |
| Collaboration équipe | **P_TEAM** | ✅ | 3 membres, rôles distincts, GitHub partagé |
| Transition écologique | **P_TEDS** | ✅ | Edge-first (inférence locale sans réseau), modèle ~8Ko (faible empreinte), réseau sollicité ponctuellement |
| Pitch oral 3 minutes | **C_ORAL** | 🔲 | À préparer (contenu disponible, démo live prête) |

### Couverture des sujets TP3/TP4

| Sujet | Attendu | Réalisé |
|---|---|---|
| TP3.1a | Collecte + visualisation temp/rhum/pres | ✅ Flask + SQLite + dashboard |
| TP3.1b | Analyse données (moyennes, stats) | ✅ `/api/stats` + Stats card + graphiques |
| TP3.1c | Alertes ThingSpeak | ⚡ Remplacé par `/api/command` downlink + admin |
| TP3.2a | Entraînement Python/TF classification météo | ✅ MLP 3 classes, SMOTE, 43 821 obs |
| TP3.2b | Compromis classes/taille/accuracy | ✅ Justifié : 3 classes, ~8Ko, 87.5% |
| TP4.1a | Export TF → ONNX → MATLAB | ⚡ Remplacé par TF → Keras direct (VPS Keras) |
| TP4.1b | Inférence MATLAB | ⚡ Remplacé par inférence Keras Flask |
| TP4.1c | Données réelles STM32 → cloud | ✅ HTTP POST STM32 → VPS → BDD → inférence |
| TP4.1d | Talkback résultat → STM32 | ⚡ Remplacé par `/api/command` one-shot |
| TP4.2a | TF → ONNX → C (X-CUBE-AI) | ✅ X-CUBE-AI utilisé (h1.c généré) |
| TP4.2b | Code C inférence STM32 | ✅ `h1_infer()` opérationnel |
| TP4.2c | Comparaison Edge vs Cloud | ✅ Tableau comparatif dans dashboard + README |
| TP4.2d | Mesure consommation puissance | 📋 Estimation datasheet STM32N6 |

> ⚡ = Fonctionnalité remplacée par une solution équivalente ou supérieure, validée par l'encadrant.

---

## Références

- [STM32N657X0 Datasheet](https://www.st.com/resource/en/datasheet/stm32n657x0.pdf)
- [X-NUCLEO-IKS01A3 — ST MEMS sensor expansion](https://www.st.com/en/ecosystems/x-nucleo-iks01a3.html)
- [Azure RTOS ThreadX documentation](https://learn.microsoft.com/en-us/azure/rtos/threadx/)
- [NetXDuo — Azure RTOS networking](https://learn.microsoft.com/en-us/azure/rtos/netx-duo/)
- [X-CUBE-AI — STM32 AI expansion pack](https://www.st.com/en/embedded-software/x-cube-ai.html)
- [Open-Meteo API — données météo historiques libres](https://open-meteo.com/)
- [Meteostat Python library](https://dev.meteostat.net/python/)
- [Chart.js documentation](https://www.chartjs.org/docs/)

---
## 👤 Auteur

William / Franck / Mostapha — L3 TRI // ESET
Module ETRS606 — IA Embarquée  
Université Savoie Mont Blanc

*README généré par William (el famoso Sovereign Lord of the TRI)*
