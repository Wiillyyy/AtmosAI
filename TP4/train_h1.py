"""
AtmosAI — Modèle H+1 embarqué
Features : uniquement temp, rhum, pres + dérivés (ce que la carte mesure réellement)
Target   : classe météo à H+1 (pas H+0)
Output   : TFLite int8 + header C pour X-Cube AI / STM32
"""

import numpy as np
import pandas as pd
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers
import requests
from datetime import datetime
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, balanced_accuracy_score
from sklearn.utils.class_weight import compute_class_weight
import joblib, json, os

# ---------------------------------------------------------------------------
# 0. GPU check
# ---------------------------------------------------------------------------
gpus = tf.config.list_physical_devices('GPU')
if gpus:
    for gpu in gpus:
        tf.config.experimental.set_memory_growth(gpu, True)
    print(f"[GPU] {len(gpus)} GPU(s) détecté(s)")
else:
    print("[GPU] CPU only")

# ---------------------------------------------------------------------------
# 1. Récupération données Open-Meteo (3 capteurs seulement)
# ---------------------------------------------------------------------------
print("\n[1/7] Téléchargement données Open-Meteo (Aix-les-Bains, 5 ans)...")

# On prend une seule station — la carte est à Aix-les-Bains
def fetch_openmeteo(lat, lon, start="2019-01-01", end="2023-12-31"):
    url = "https://archive-api.open-meteo.com/v1/archive"
    params = {
        "latitude":   lat,
        "longitude":  lon,
        "start_date": start,
        "end_date":   end,
        "hourly": ",".join([
            "temperature_2m",
            "relativehumidity_2m",
            "surface_pressure",
            "weathercode",
        ]),
        "timezone": "UTC",
    }
    r = requests.get(url, params=params, timeout=120)
    r.raise_for_status()
    h = r.json()["hourly"]
    df = pd.DataFrame(h)
    df["time"] = pd.to_datetime(df["time"])
    df = df.set_index("time")
    df = df.rename(columns={
        "temperature_2m":      "temp",
        "relativehumidity_2m": "rhum",
        "surface_pressure":    "pres",
        "weathercode":         "coco",
    })
    return df

data = fetch_openmeteo(45.6886, 5.9153)
print(f"     {len(data):,} observations récupérées")

# ---------------------------------------------------------------------------
# 2. Labellisation WMO -> 3 classes
# ---------------------------------------------------------------------------
print("\n[2/7] Labellisation...")

WMO_MAP = {
    0: 0, 1: 0,                                    # Clair
    45: 2, 48: 2,                                  # Brouillard
    51: 1, 53: 1, 55: 1, 56: 1, 57: 1,            # Bruine
    61: 1, 63: 1, 65: 1, 66: 1, 67: 1,            # Pluie
    71: 1, 73: 1, 75: 1, 77: 1,                    # Neige
    80: 1, 81: 1, 82: 1, 85: 1, 86: 1,            # Averses
}

labels_names = {0: "Clair", 1: "Pluie", 2: "Brouillard"}

data = data.dropna(subset=["coco"])
data["coco_int"] = data["coco"].astype(int)
data = data[data["coco_int"].isin(WMO_MAP.keys())].copy()
data["label_now"] = data["coco_int"].map(WMO_MAP)

# --- CLEF : le label est celui de H+1, pas H+0 ---
data["label"] = data["label_now"].shift(-1)
data = data.dropna(subset=["label"])
data["label"] = data["label"].astype(int)

dist = data["label"].value_counts().sort_index()
print("     Distribution H+1 :")
for idx, count in dist.items():
    print(f"       {labels_names[idx]:12s} : {count:6d} ({100*count/len(data):.1f}%)")

# ---------------------------------------------------------------------------
# 3. Feature engineering — uniquement ce que la carte mesure
# ---------------------------------------------------------------------------
print("\n[3/7] Feature engineering (3 capteurs réels)...")

data["temp"] = data["temp"].interpolate()
data["rhum"] = data["rhum"].interpolate()
data["pres"] = data["pres"].interpolate()

# Cycliques temporels
data["hour"]  = data.index.hour
data["month"] = data.index.month
data["hour_sin"]  = np.sin(2 * np.pi * data["hour"]  / 24)
data["hour_cos"]  = np.cos(2 * np.pi * data["hour"]  / 24)
data["month_sin"] = np.sin(2 * np.pi * data["month"] / 12)
data["month_cos"] = np.cos(2 * np.pi * data["month"] / 12)

# Deltas — tendances (les plus prédictifs pour H+1)
data["temp_delta_1h"] = data["temp"].diff(1)
data["temp_delta_3h"] = data["temp"].diff(3)
data["pres_delta_1h"] = data["pres"].diff(1)
data["pres_delta_3h"] = data["pres"].diff(3)
data["rhum_delta_1h"] = data["rhum"].diff(1)

# Point de rosée approché (temp - rhum/5)
data["temp_dwpt_diff"] = data["temp"] - ((100 - data["rhum"]) / 5.0)

FEATURES = [
    # Mesures brutes capteurs
    "temp", "rhum", "pres",
    # Tendances — clé pour H+1
    "temp_delta_1h", "temp_delta_3h",
    "pres_delta_1h", "pres_delta_3h",
    "rhum_delta_1h",
    # Cycliques
    "hour_sin", "hour_cos",
    "month_sin", "month_cos",
    # Dérivé
    "temp_dwpt_diff",
]

data = data.dropna(subset=FEATURES)
print(f"     {len(data):,} observations | {len(FEATURES)} features")
print(f"     Features : {FEATURES}")

# ---------------------------------------------------------------------------
# 4. SMOTE
# ---------------------------------------------------------------------------
print("\n[4/7] SMOTE...")

X_raw = data[FEATURES].values.astype(np.float32)
y_raw = data["label"].values.astype(np.int32)

try:
    from imblearn.over_sampling import SMOTE
    sm = SMOTE(random_state=42, k_neighbors=5)
    X_res, y_res = sm.fit_resample(X_raw, y_raw)
    print(f"     Avant : {len(X_raw):,} | Après : {len(X_res):,}")
except ImportError:
    print("     SMOTE non disponible — ignoré")
    X_res, y_res = X_raw, y_raw

# ---------------------------------------------------------------------------
# 5. Split + normalisation
# ---------------------------------------------------------------------------
print("\n[5/7] Split + normalisation...")

X_train, X_test, y_train, y_test = train_test_split(
    X_res, y_res, test_size=0.15, random_state=42, stratify=y_res
)

scaler  = StandardScaler()
X_train = scaler.fit_transform(X_train)
X_test  = scaler.transform(X_test)

print(f"     Train: {len(X_train):,} | Test: {len(X_test):,}")

# ---------------------------------------------------------------------------
# 6. Modèle MLP léger (embarquable sur STM32)
# ---------------------------------------------------------------------------
print("\n[6/7] Construction modèle MLP léger...")

# Volontairement petit — doit rentrer sur la carte
model = keras.Sequential([
    keras.layers.Input(shape=(len(FEATURES),)),
    keras.layers.Dense(32, activation="relu"),
    keras.layers.BatchNormalization(),
    keras.layers.Dense(32, activation="relu"),
    keras.layers.Dense(16, activation="relu"),
    keras.layers.Dense(3,  activation="softmax"),
], name="meteo_h1_embedded")

#Fonction d'activation : Relu, Avec 3 couches de neuronnes
#Relu idéal pour les relations complexes avec plusieures variables
#Dense = Fully connected, combinne toutes les infos
#Couche 1 : signaux de base 
#Couche 2 : interaction entre les signaux (humidité, pression...)
#Couche 3 : Classification

model.summary()

classes     = np.unique(y_train)
weights     = compute_class_weight("balanced", classes=classes, y=y_train)
class_weight = dict(zip(classes, weights))
print(f"\n     Class weights : { {labels_names[k]: round(v,3) for k,v in class_weight.items()} }")

model.compile(
    optimizer = keras.optimizers.Adam(learning_rate=1e-3),
    loss      = "sparse_categorical_crossentropy",
    metrics   = ["accuracy"],
)

#optimizer : Adam , vitesse d'apprentissage : 0.001
#Adam, optimizer intelligent et adaptatif
#Fontion de coût : sparse_categorical_crossentropy
#sparse_categorical_crossentropy idéal pour classification multi-classes


callbacks = [
    keras.callbacks.EarlyStopping(
        monitor="val_accuracy", patience=15,
        restore_best_weights=True, verbose=1,
    ),
    keras.callbacks.ReduceLROnPlateau(
        monitor="val_loss", factor=0.5, patience=5,
        min_lr=1e-6, verbose=1,
    ),
]

#Earlystoping : stop si pas d’amélioration pendant 15 epochs

history = model.fit(
    X_train, y_train,
    validation_split = 0.15,
    epochs           = 200,
    batch_size       = 256,
    callbacks        = callbacks,
    class_weight     = class_weight,
    verbose          = 1,
)

#Batch size = 256 exemples. On prend 5 ans de météo , 5 x 365 x 24 = 43 800 
# 1 epoch = analyse des 43 800 données en groupe de 256

# ---------------------------------------------------------------------------
# 7. Évaluation
# ---------------------------------------------------------------------------
print("\n[7/7] Évaluation...")

loss, acc = model.evaluate(X_test, y_test, verbose=0)
y_pred    = np.argmax(model.predict(X_test, verbose=0), axis=1)
bal_acc   = balanced_accuracy_score(y_test, y_pred)

print(f"\n     Test accuracy     : {acc*100:.2f}%")
print(f"     Balanced accuracy : {bal_acc*100:.2f}%")
print(f"     Epochs            : {len(history.history['loss'])}")
print("\nRapport :")
print(classification_report(y_test, y_pred,
      target_names=["Clair", "Pluie", "Brouillard"],
      labels=[0,1,2], zero_division=0))

# ---------------------------------------------------------------------------
# 8. Sauvegarde Keras + TFLite int8 + Header C
# ---------------------------------------------------------------------------
print("\nSauvegarde...")

os.makedirs("model_h1", exist_ok=True)

# Keras
model.save("model_h1/meteo_h1_model.keras")
joblib.dump(scaler, "model_h1/scaler_h1.pkl")

# TFLite quantisé int8
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

with open("model_h1/meteo_h1_model.tflite", "wb") as f:
    f.write(tflite_model)

tflite_size = len(tflite_model)
print(f"     TFLite int8 : {tflite_size/1024:.1f} Ko")

# Header C pour X-Cube AI / TFLite Micro
with open("model_h1/meteo_h1_model.h", "w") as f:
    f.write("/* AtmosAI — Modèle H+1 embarqué — généré automatiquement */\n")
    f.write("#pragma once\n\n")
    f.write(f"/* Taille : {tflite_size} bytes ({tflite_size/1024:.1f} Ko) */\n")
    f.write(f"/* Features : {len(FEATURES)} | Classes : Clair/Pluie/Brouillard */\n\n")
    f.write(f"const unsigned int meteo_h1_model_len = {tflite_size};\n\n")
    f.write("const unsigned char meteo_h1_model[] = {\n  ")
    hex_vals = [f"0x{b:02x}" for b in tflite_model]
    lines    = [", ".join(hex_vals[i:i+16]) for i in range(0, len(hex_vals), 16)]
    f.write(",\n  ".join(lines))
    f.write("\n};\n")

# Métadonnées
meta = {
    "model":             "meteo_h1_embedded",
    "target":            "H+1",
    "classes":           ["Clair", "Pluie", "Brouillard"],
    "features":          FEATURES,
    "n_features":        len(FEATURES),
    "test_accuracy":     float(acc),
    "balanced_accuracy": float(bal_acc),
    "epochs_trained":    len(history.history["loss"]),
    "tflite_size_bytes": tflite_size,
    "scaler_mean":       scaler.mean_.tolist(),
    "scaler_scale":      scaler.scale_.tolist(),
}
with open("model_h1/meta_h1.json", "w") as f:
    json.dump(meta, f, indent=2)

print(f"\n{'='*50}")
print(f"  Target           : H+1")
print(f"  Accuracy         : {acc*100:.2f}%")
print(f"  Balanced Acc     : {bal_acc*100:.2f}%")
print(f"  Features         : {len(FEATURES)}")
print(f"  TFLite size      : {tflite_size/1024:.1f} Ko")
print(f"  Flash N657X0     : 512 Ko → {'✅ OK' if tflite_size < 512*1024 else '❌ trop grand'}")
print(f"{'='*50}")
print(f"\n✅ Fichiers dans ./model_h1/")
print(f"   - meteo_h1_model.keras")
print(f"   - meteo_h1_model.tflite")
print(f"   - meteo_h1_model.h  ← à importer dans STM32CubeIDE")
print(f"   - scaler_h1.pkl")
print(f"   - meta_h1.json")
