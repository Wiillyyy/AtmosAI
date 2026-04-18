"""
AtmosAI — Modèles J+1 / J+2 / J+3 côté VPS
Features : uniquement ce que la carte mesure (temp, rhum, pres + dérivés)
Entraîne 3 modèles indépendants pour 24h, 48h, 72h.
"""

import numpy as np
import pandas as pd
import tensorflow as tf
from tensorflow import keras
import requests
from datetime import datetime
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, balanced_accuracy_score
from sklearn.utils.class_weight import compute_class_weight
import joblib, json, os

# ---------------------------------------------------------------------------
# 0. GPU
# ---------------------------------------------------------------------------
gpus = tf.config.list_physical_devices('GPU')
print(f"[GPU] {len(gpus)} GPU(s)" if gpus else "[GPU] CPU only")

# ---------------------------------------------------------------------------
# 1. Données Open-Meteo (mêmes paramètres que train_h1.py)
# ---------------------------------------------------------------------------
print("\n[1/6] Téléchargement Open-Meteo (Aix-les-Bains, 5 ans)...")

def fetch_openmeteo(lat, lon, start="2019-01-01", end="2023-12-31"):
    url = "https://archive-api.open-meteo.com/v1/archive"
    params = {
        "latitude":   lat,
        "longitude":  lon,
        "start_date": start,
        "end_date":   end,
        "hourly": "temperature_2m,relativehumidity_2m,surface_pressure,weathercode",
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
print(f"     {len(data):,} observations")

# ---------------------------------------------------------------------------
# 2. Labellisation WMO — mêmes classes que H+1
# ---------------------------------------------------------------------------
print("\n[2/6] Labellisation...")

WMO_MAP = {
    0: 0, 1: 0, 2: 0, 3: 0,            # Clair / partiellement nuageux / couvert
    51: 1, 53: 1, 55: 1, 56: 1, 57: 1,  # Bruine → Pluie
    61: 1, 63: 1, 65: 1, 66: 1, 67: 1,  # Pluie
    80: 1, 81: 1, 82: 1, 85: 1, 86: 1,  # Averses → Pluie
    95: 1, 96: 1, 99: 1,                 # Orages → Pluie
    71: 2, 73: 2, 75: 2, 77: 2,          # Neige
    # Brouillard (45/48) absent des données ERA5 grillées — ignoré
}
LABELS = {0: "Clair", 1: "Pluie", 2: "Neige"}

data = data.dropna(subset=["coco"])
data["coco_int"] = data["coco"].astype(int)
data = data[data["coco_int"].isin(WMO_MAP.keys())].copy()
data["label_now"] = data["coco_int"].map(WMO_MAP)

# ---------------------------------------------------------------------------
# 3. Feature engineering — identique à la carte (13 features)
# ---------------------------------------------------------------------------
print("\n[3/6] Feature engineering...")

data["temp"] = data["temp"].interpolate()
data["rhum"] = data["rhum"].interpolate()
data["pres"] = data["pres"].interpolate()

data["hour"]  = data.index.hour
data["month"] = data.index.month
data["hour_sin"]  = np.sin(2 * np.pi * data["hour"]  / 24)
data["hour_cos"]  = np.cos(2 * np.pi * data["hour"]  / 24)
data["month_sin"] = np.sin(2 * np.pi * data["month"] / 12)
data["month_cos"] = np.cos(2 * np.pi * data["month"] / 12)

data["temp_delta_1h"] = data["temp"].diff(1)
data["temp_delta_3h"] = data["temp"].diff(3)
data["pres_delta_1h"] = data["pres"].diff(1)
data["pres_delta_3h"] = data["pres"].diff(3)
data["rhum_delta_1h"] = data["rhum"].diff(1)

# Magnus dewpoint (identique à h1_inference.c)
_a, _b = 17.625, 243.04
_rhum  = data["rhum"].clip(lower=0.1)
_alpha = np.log(_rhum / 100.0) + (_a * data["temp"]) / (_b + data["temp"])
data["temp_dwpt_diff"] = data["temp"] - (_b * _alpha) / (_a - _alpha)

FEATURES = [
    "temp", "rhum", "pres",
    "temp_delta_1h", "temp_delta_3h",
    "pres_delta_1h", "pres_delta_3h",
    "rhum_delta_1h",
    "hour_sin", "hour_cos",
    "month_sin", "month_cos",
    "temp_dwpt_diff",
]

data = data.dropna(subset=FEATURES + ["label_now"])
print(f"     {len(data):,} observations | {len(FEATURES)} features")

# ---------------------------------------------------------------------------
# 4. Entraînement d'un modèle par horizon
# ---------------------------------------------------------------------------
HORIZONS = {
    "j1": 24,   # J+1 — dans 24 heures
    "j2": 48,   # J+2 — dans 48 heures
    "j3": 72,   # J+3 — dans 72 heures
}

for name, shift in HORIZONS.items():
    print(f"\n{'='*55}")
    print(f"  Horizon {name.upper()} (shift={shift}h)")
    print(f"{'='*55}")

    df = data.copy()
    df["label"] = df["label_now"].shift(-shift)
    df = df.dropna(subset=["label"])
    df["label"] = df["label"].astype(int)

    dist = df["label"].value_counts().sort_index()
    print("  Distribution :")
    for idx, count in dist.items():
        print(f"    {LABELS[idx]:12s} : {count:6d} ({100*count/len(df):.1f}%)")

    X = df[FEATURES].values.astype(np.float32)
    y = df["label"].values.astype(np.int32)

    # SMOTE si dispo
    try:
        from imblearn.over_sampling import SMOTE
        sm = SMOTE(random_state=42, k_neighbors=5)
        X, y = sm.fit_resample(X, y)
        print(f"  Après SMOTE : {len(X):,}")
    except ImportError:
        print("  SMOTE non disponible — ignoré")

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.15, random_state=42, stratify=y
    )

    scaler  = StandardScaler()
    X_train = scaler.fit_transform(X_train)
    X_test  = scaler.transform(X_test)

    classes = np.unique(y_train)
    weights = compute_class_weight("balanced", classes=classes, y=y_train)
    cw = dict(zip(classes, weights))

    model = keras.Sequential([
        keras.layers.Input(shape=(len(FEATURES),)),
        keras.layers.Dense(64, activation="relu"),
        keras.layers.BatchNormalization(),
        keras.layers.Dense(64, activation="relu"),
        keras.layers.Dense(32, activation="relu"),
        keras.layers.Dense(3,  activation="softmax"),
    ], name=f"meteo_{name}")

    model.compile(
        optimizer=keras.optimizers.Adam(1e-3),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    callbacks = [
        keras.callbacks.EarlyStopping(
            monitor="val_accuracy", patience=15,
            restore_best_weights=True, verbose=0,
        ),
        keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss", factor=0.5, patience=5,
            min_lr=1e-6, verbose=0,
        ),
    ]

    history = model.fit(
        X_train, y_train,
        validation_split=0.15,
        epochs=200,
        batch_size=256,
        callbacks=callbacks,
        class_weight=cw,
        verbose=0,
    )

    loss, acc = model.evaluate(X_test, y_test, verbose=0)
    y_pred  = np.argmax(model.predict(X_test, verbose=0), axis=1)
    bal_acc = balanced_accuracy_score(y_test, y_pred)

    print(f"\n  Test accuracy     : {acc*100:.2f}%")
    print(f"  Balanced accuracy : {bal_acc*100:.2f}%")
    print(f"  Epochs            : {len(history.history['loss'])}")
    print(f"\n{classification_report(y_test, y_pred, target_names=['Clair','Pluie','Neige'], labels=[0,1,2], zero_division=0)}")

    # Sauvegarde
    out_dir = f"model_{name}"
    os.makedirs(out_dir, exist_ok=True)
    model.save(f"{out_dir}/meteo_{name}_model.keras")
    joblib.dump(scaler, f"{out_dir}/scaler_{name}.pkl")

    meta = {
        "horizon_hours":    shift,
        "classes":          ["Clair", "Pluie", "Neige"],
        "features":         FEATURES,
        "n_features":       len(FEATURES),
        "test_accuracy":    float(acc),
        "balanced_accuracy": float(bal_acc),
        "epochs_trained":   len(history.history["loss"]),
        "scaler_mean":      scaler.mean_.tolist(),
        "scaler_scale":     scaler.scale_.tolist(),
    }
    with open(f"{out_dir}/meta_{name}.json", "w") as f:
        json.dump(meta, f, indent=2)

    print(f"  Sauvegarde : ./{out_dir}/")

print("\n" + "="*55)
print("  DONE — modeles dans ./model_j1/ ./model_j2/ ./model_j3/")
print("="*55)
