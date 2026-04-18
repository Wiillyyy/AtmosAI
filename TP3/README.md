# ⛰ AtmosAI — Station Météo Embarquée

> **ETRS606 — IA Embarquée · L3 Réseaux & Télécoms**  
> Collecte de données capteurs via STM32, classification météo par réseau de neurones, visualisation temps réel sur VPS personnel.

---

## 📐 Architecture

```
[Nucleo STM32 N657X0]
  └── Capteurs I2C (HTS221, LPS22HH, LSM6DSO...)
       └── HTTP POST (JSON)
            └── [VPS — Flask API + SQLite]
                 ├── Inférence ResNet (prédiction météo)
                 └── Dashboard HTML (atmosai.willydev.xyz)
```

---

## 📁 Structure du projet

```
meteo_api/
├── app.py                  # API Flask (endpoints REST)
├── database.py             # Init SQLite + helpers
├── requirements.txt        # Dépendances Python
├── train.py                # Entraînement MLP v1 (baseline)
├── trainv2.py              # Entraînement MLP v2 (feature engineering)
├── trainv3.py              # Entraînement MLP v3 (4 classes)
├── trainv4.py              # Entraînement MLP v4 (3 classes séparées)
├── train_ultimate.py       # Entraînement ResNet (99.84% accuracy)
├── simulator.py            # Simulateur capteurs (remplace la Nucleo)
├── index.html              # Dashboard web temps réel
├── meteo_api.service       # Service systemd
└── model/
    ├── meteo_model.keras   # Modèle ResNet sauvegardé
    ├── meteo_model.tflite  # Modèle quantisé int8 pour STM32
    ├── meteo_model.h       # Header C pour TFLite Micro
    ├── scaler.pkl          # StandardScaler fitted
    └── meta.json           # Métadonnées (classes, features, accuracy)
```

---

## 🚀 Installation sur VPS

### Prérequis
- Python 3.9+
- Caddy avec HTTPS
- Domaine pointé sur le VPS

### 1. Cloner et installer

```bash
git clone <ton-repo>
cd meteo_api

python3 -m venv venv
venv/bin/pip install -r requirements.txt
```

### 2. Initialiser la base de données

```bash
venv/bin/python -c "from database import init_db; init_db()"
```

### 3. Service systemd

```bash
sudo cp meteo_api.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now meteo_api
```

> ⚠️ Changer `METEO_API_KEY` dans le fichier `.service` avant de démarrer.

### 4. Caddyfile

```caddyfile
atmosai.willydev.xyz {
    handle /api/* {
        reverse_proxy 127.0.0.1:5001
    }
    handle /health {
        reverse_proxy 127.0.0.1:5001
    }
    handle {
        root * /usr/share/caddy/atmosai
        file_server
    }
}
```

```bash
sudo systemctl reload caddy
sudo cp index.html /usr/share/caddy/atmosai/index.html
```

---

## 🧠 Modèle IA

### Évolution des versions

| Version | Architecture | Classes | Accuracy |
|---------|-------------|---------|----------|
| v1 | MLP 2 couches | 3 | 65% |
| v2 | MLP + feature engineering (21 features) | 3 | 76% |
| v3 | MLP + 4 classes | 4 | 68% |
| v4 | MLP + classes séparées | 3 | 81% |
| **ultimate** | **ResNet tabulaire + SMOTE** | **3** | **99.84%** | ** non utilisé mais testé**

### Données
- Source : **Meteostat** (API gratuite, pas de clé requise)
- Stations : Aix-les-Bains
- Période : **2014–2023** (10 ans)
- Volume : **~260 000** observations (samples)

### Classes
| ID | Classe | Catégorie TP3 | Signature capteurs |
|----|--------|--------------|-------------------|
| 0 | ☀️ Clair | Ciel dégagé | rhum↓ pres↑ prcp=0 |
| 1 | 🌧️ Pluie | Précipitations | prcp>0 pres↓ rhum↑ |
| 2 | 🌫️ Brouillard | Phénomène particulier | rhum~100% wind~0 prcp=0 |

> Les classes ambiguës (nuageux, orage) ont été volontairement exclues pour maximiser la séparabilité inter-classe.

### Feature engineering (32 features)

| Catégorie | Features | Justification |
|-----------|----------|--------------|
| Mesures brutes | `temp`, `rhum`, `pres`, `prcp`, `wspd`, `dwpt`, `tsun` | Observations directes capteurs |
| Vent cartésien | `wdir_sin`, `wdir_cos` | Évite la discontinuité 359°→0° |
| Cycliques | `hour_sin/cos`, `month_sin/cos`, `doy_sin/cos` | Encode le temps sans discontinuité |
| Deltas thermiques | `temp_delta_1h/3h/6h/12h` | Tendance thermique, détection de fronts |
| Deltas baromètre | `pres_delta_1h/3h/6h/12h` | Chute de pression = front qui arrive |
| Accumulations | `prcp_3h/6h/12h/24h` | Persistance des précipitations |
| Rolling | `rhum_6h`, `rhum_12h`, `wspd_6h` | Contexte temporel humidité/vent |
| Physiques | `temp_dwpt_diff`, `wind_power` | Point de rosée relatif, énergie cinétique vent |

### Architecture ResNet tabulaire

```
Input(32)
  → Dense(256) + BatchNorm + ReLU
  → ResBlock(256) x4  ← connexions skip résiduelles
  → Dense(128, ReLU) + Dropout(0.2)
  → Dense(3, Softmax)
```

**Pourquoi ResNet sur données tabulaires ?**
Les connexions résiduelles permettent au gradient de circuler sans dégradation sur les couches profondes, là où un MLP classique stagnerait. Particulièrement efficace quand les features ont des importances très inégales.

### Hyperparamètres

| Paramètre | Valeur | Justification |
|-----------|--------|--------------|
| Optimizer | AdamW (lr=1e-3) | Weight decay L2 découplée du lr |
| Weight decay | 1e-4 | Régularisation sans affecter le lr |
| Batch size | 512 | Exploite les cœurs CPU/GPU |
| Epochs max | 500 | EarlyStopping patience=20 |
| ReduceLROnPlateau | factor=0.4, patience=7 | Affinage fin du lr |
| Dropout | 0.2 | Régularisation légère |
| SMOTE | k_neighbors=5 | Rééquilibrage classe Brouillard |

### Résultats finaux

```
Test accuracy     : 99.84%
Balanced accuracy : 99.84%
Epochs entraînés  : 47 / 500
```

### Entraînement

```bash
# Sur machine avec Python 3.12 + venv propre
python3 -m venv venv_meteo
source venv_meteo/bin/activate
pip install tensorflow scikit-learn imbalanced-learn joblib requests pandas

python3 train_ultimate.py
```

### Conversion TFLite pour STM32

```bash
venv/bin/python - <<'EOF'
import tensorflow as tf

model = tf.keras.models.load_model("model/meteo_model.keras")
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

with open("model/meteo_model.tflite", "wb") as f:
    f.write(tflite_model)
print(f"Taille : {len(tflite_model)/1024:.1f} Ko")
EOF
```

| Format | Taille | Flash N657X0 (512 Ko) |
|--------|--------|-----------------------|
| Keras float32 | ~120 Ko | ✅ |
| TFLite int8 | ~30 Ko | ✅ largement |

---

## 📡 API REST

### `POST /api/data`

**Headers**
```
Content-Type: application/json
X-API-Key: <atmosai_w1lly_2026>
```

**Body**
```json
{
  "device_id": "nucleo",
  "temperature": 14.5,
  "humidity": 72.0,
  "pressure": 1011.3,
  "precipitation": 0.0,
  "wind_speed": 8.5
}
```

**Réponse**
```json
{
  "status": "ok",
  "timestamp": "2026-03-30T12:00:00.000000",
  "received": { "..." },
  "prediction": "Brouillard",
  "confidence": 0.997
}
```

### `GET /api/data?n=50`
Retourne les N dernières mesures.

### `GET /api/stats`
Statistiques globales.

### `GET /health`
Liveness check.

---

## 🖥️ Simulateur

```bash
python3 simulator.py
```

Simule des données capteurs réalistes (cycle jour/nuit, variation de pression, épisodes de pluie) — envoie à l'API toutes les 10 secondes. Remplace la Nucleo STM32 pour tester la chaîne complète.

```
🚀 Simulateur démarré — envoi vers https://atmosai.willydev.xyz/api/data
[12:35:01] ✅ #0001 | T= 8.3°C  H= 96.5%  P= 1008.1hPa  → Brouillard (99.7%)
```

---

## Intégration STM32 (TP2 → TP3)

La Nucleo doit envoyer un `HTTP POST` vers `/api/data` avec le même format JSON que le simulateur. Capteurs utilisés :

| Capteur | Grandeur | Driver |
|---------|----------|--------|
| HTS221 | Température + Humidité | `hts221_reg.c` |
| LPS22HH | Pression | `lps22hh_reg.c` |
| LSM6DSO | Accélération + Gyroscope | `lsm6dso_reg.c` |

Le modèle TFLite peut être embarqué directement via **TFLite Micro** en incluant `model/meteo_model.h` dans STM32CubeIDE.

---

## Justification des choix

**Pourquoi VPS plutôt que ThingSpeak ?**
Contrôle total des données, pas de limite de débit, inférence IA intégrée côté serveur, dashboard personnalisé.

**Pourquoi ResNet plutôt que MLP ?**
Les connexions résiduelles résolvent le vanishing gradient sur les couches profondes. Sur ce dataset, le gain est spectaculaire : 81% (MLP v4) → 99.84% (ResNet).

**Pourquoi 3 classes séparées ?**
Maximiser la séparabilité inter-classe dans l'espace features : Clair (rhum basse), Pluie (prcp>0), Brouillard (rhum~100% + vent nul). Les classes ambiguës comme Nuageux dégradent les performances sans apport métier.

**Pourquoi Open-Meteo ?**
API gratuite, sans clé, sans limite de débit raisonnable, 10 ans d'historique, compatible Python 3.12 sans dépendances problématiques.

---

## 👤 Auteur

William / Franck / Mostapha — L3 TRI // ESET 
Module ETRS606 — IA Embarquée  
Université Savoie Mont Blanc
