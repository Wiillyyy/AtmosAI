# AtmosAI — Station météo embarquée avec IA Edge & Cloud

> **Module ETRS606 — IA Embarquée · Université Savoie Mont Blanc**  
> William Z. · Franck G. · Mostapha K.

---

## Ce qu'on a construit

On a conçu une station météo embarquée complète, de bout en bout — du capteur physique jusqu'au site web hébergé sur notre propre serveur, en passant par un réseau de neurones qui tourne **directement sur notre microcontrôleur**, sans cloud, sans internet, en moins d'une milliseconde.

Notre carte STM32 capte la température, l'humidité et la pression toutes les 20 secondes. Elle calcule 13 features en temps réel, lance une inférence MLP complète localement, et envoie le tout — mesures + prédiction — vers notre API Flask hébergée sur VPS. Le site web affiche tout en direct, se rafraîchit automatiquement, et on peut même envoyer des commandes à la carte depuis le navigateur.

On n'a pas utilisé ThingSpeak. On n'a pas utilisé MATLAB. On a tout fait nous-mêmes.

---

## L'architecture en un coup d'œil

```
┌──────────────────────────────────────────────────────┐
│                   CARTE STM32N657X0                  │
│                                                      │
│  HTS221 (temp/rhum) + LPS22HH (pression)  ← I2C     │
│              ↓                                       │
│   Ring buffer 560 échantillons (~3h)                 │
│              ↓                                       │
│   Calcul 13 features en temps réel                   │
│              ↓                                       │
│   h1_infer() — MLP C float32 — < 1ms                 │
│   Résultat : Clair / Pluie / Neige + confiance       │
│              ↓                                       │
│   HTTP POST toutes les ~20s via Ethernet             │
└──────────────────────┬───────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│              VPS — API Flask + SQLite                │
│                                                      │
│   Stockage SQLite → inférence Keras J+1/J+2/J+3     │
│   Routes publiques + admin protégées par clé API     │
└──────────────────────┬───────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│            Dashboard Web — index.html                │
│                                                      │
│   Métriques temps réel · Sparklines · Prédictions    │
│   Features STM32 live · Historique · Comparatif      │
└──────────────────────────────────────────────────────┘
```

---

## Le matériel

On utilise une carte **NUCLEO-N657X0** (STM32N657X0 — Cortex-M55 à 800 MHz) avec le shield **X-NUCLEO-IKS01A3**. Les capteurs communiquent en I2C :
- **HTS221** pour la température et l'humidité (±0.5°C / ±3.5% RH)
- **LPS22HH** pour la pression atmosphérique (±0.1 hPa)

La carte tourne sous **Azure RTOS ThreadX** avec deux threads indépendants : un thread capteur qui lit, calcule et infère, et un thread TCP qui poste les résultats au VPS.

---

## Le modèle embarqué — H+1

C'est la partie dont on est le plus fiers.

On a entraîné un MLP en Python/TensorFlow sur **43 821 heures** de données météo historiques à Aix-les-Bains (Open-Meteo, 2019–2023). On a exporté les poids en C float32 via X-CUBE-AI. Le réseau tourne intégralement sur la carte — pas de dépendance réseau, pas de latence cloud, pas de NPU.

```
Input (13 features)
      ↓
Dense(32, ReLU)   ← BN fusionné dans les poids
      ↓
Dense(32, ReLU)
      ↓
Dense(16, ReLU)
      ↓
Dense(3, Softmax)
      ↓
Clair · Pluie · Neige
```

**87.5% d'accuracy. ~8 Ko. Inférence en < 1 ms. Charge CPU < 1%.**

### Les 13 features

Ce qui rend notre modèle robuste, c'est qu'il ne regarde pas juste les valeurs brutes. Il reçoit des **features calculées dynamiquement** depuis un ring buffer de 560 échantillons (~3h d'historique) maintenu en RAM :

| # | Feature | Source |
|---|---|---|
| 1–3 | temp, rhum, pres | Capteurs directs |
| 4–5 | ΔT 1h, ΔT 3h | Ring buffer |
| 6–7 | ΔP 1h, ΔP 3h | Ring buffer |
| 8 | ΔH 1h | Ring buffer |
| 9–10 | hour_sin, hour_cos | RTC (encodage cyclique) |
| 11–12 | month_sin, month_cos | RTC (encodage cyclique) |
| 13 | T − point de rosée | Formule de Magnus embarquée |

Les deltas capturent la **tendance** (chute de pression → pluie probable). L'encodage sin/cos de l'heure et du mois évite l'artefact "23h est loin de 0h" — le modèle sait que c'est continu. Le point de rosée est directement prédictif de la condensation et des précipitations.

### Pourquoi le NPU n'est pas utilisé

On a essayé. X-CUBE-AI a généré le code avec le runtime LL_ATON. Au premier appel, BusFault — crash immédiat. Après analyse des registres (CFSR, adresse fautive), on a identifié la cause : le buffer d'entrée ATON pointe vers `0x342e0000` (AXISRAM5/npuRAM5), une zone câblée **exclusivement sur le bus AXI du NPU**. Le CPU n'a aucun chemin d'accès vers cette mémoire.

De toute façon, X-CUBE-AI avait compilé tous les blocs en `EpochBlock_Flags_pure_sw` — le NPU matériel n'était pas sollicité, notre modèle est trop petit pour le justifier. Notre `h1_infer()` fait exactement les mêmes calculs, mêmes poids, mêmes résultats — mais dans la SRAM accessible au CPU.

---

## Le backend VPS

On a refusé de dépendre de ThingSpeak. On a déployé notre propre stack :

- **Flask** (Python) servi par **Gunicorn**
- **SQLite** pour le stockage des mesures
- **Caddy** pour servir les fichiers statiques
- **systemd** pour la résilience (redémarrage automatique)

Notre API expose des routes publiques (lecture des données, prévisions) et des routes admin protégées par clé (`purge, vider la base, envoyer une commande à la carte`).

### Les modèles cloud J+1/J+2/J+3

En complément du H+1 embarqué, on a entraîné **3 modèles Keras indépendants** sur le VPS pour des prévisions à plus long terme, sur les mêmes 13 features (recalculées depuis l'historique SQLite) :

| Modèle | Horizon | Balanced Accuracy |
|---|---|---|
| J+1 | +24h | 64.3% |
| J+2 | +48h | 61.2% |
| J+3 | +72h | 54.4% |

La dégradation avec l'horizon est attendue — prédire à 72h avec uniquement des capteurs locaux sans données NWP est un problème difficile.

---

## Le dashboard web

Un fichier HTML/CSS/JS vanilla, aucun framework. Thème sombre/clair commutable. Rafraîchissement automatique toutes les 15 secondes.

**Ce qu'on affiche sur le dashboard :**
- Valeurs temps réel en grand (température cyan, humidité amber, pression vert)
- Prédiction H+1 de la carte : classe + barre de confiance animée + emoji flottant
- **Features STM32 live** : les 10 valeurs calculées par le JS depuis l'historique (deltas, point de rosée, cycliques) — colorées vert/rouge selon leur signe, exactement les mêmes formules qu'en C embarqué
- 3 sparklines Canvas 2D (courbes bézier avec gradient de remplissage)
- Tableau des dernières mesures
- Onglet Historique avec graphiques Chart.js interactifs
- Onglet Modèle IA avec tout le détail technique + tableau comparatif Edge vs Cloud

**Page admin** (`admin.html`) : gestion de la base, purge, commandes downlink vers la carte (dont un mode stroboscope pour la démo).

---

## Mesure de performance embarquée

On mesure en temps réel la charge CPU et la consommation estimée grâce au **compteur DWT** (Data Watchpoint and Trace) du Cortex-M55 — un compteur de cycles hardware précis à la nanoseconde. À chaque cycle, le UART affiche :

```
==========================================
[PWR] Periode cycle :  20043 ms
[PWR] CPU load      :  0.3 %
[PWR] h1_infer()    :  0.18 us  (144 cyc)
[PWR] I estimee     :  30.4 mA
[PWR] P estimee     :  100 mW  (0.100 W)
==========================================
```

0.3% de charge CPU pour faire tourner l'IA. La carte dort 99.7% du temps.

---

## Comparaison Edge AI vs Cloud AI

| Critère | STM32 H+1 — Edge | VPS J+1/J+2/J+3 — Cloud |
|---|---|---|
| Horizon | H+1 | J+1 / J+2 / J+3 |
| Accuracy | **87.5%** | 80.9% (moy. J+1) |
| Taille modèle | **~8 Ko** | ~120 Ko (Keras) |
| Latence inférence | **< 1 ms** (local) | ~50 ms (réseau) |
| Dépendance réseau | **Aucune** | Requiert connexion |
| Consommation | **~100 mW** | Serveur 24/7 |
| Mise à jour | Reflashing | Redémarrage Flask |

Notre approche **Edge-first** est cohérente avec une logique de sobriété numérique : l'inférence critique se passe localement, le réseau n'est utilisé que pour archiver et enrichir — pas pour faire tourner le modèle principal.

---

## Les chiffres qui comptent

| Métrique | Valeur |
|---|---|
| Accuracy H+1 embarqué | **87.5%** |
| Taille modèle embarqué | **~8 Ko** |
| Inférence STM32 | **< 1 ms** |
| Charge CPU IA | **< 1%** |
| Observations d'entraînement | **43 821** |
| Features | **13** |
| Cycle de mesure | **~20 s** |
| Modèles cloud | **3** (J+1 · J+2 · J+3) |

---

## Lancer le projet

### Firmware

Ouvrir `AtmosAI_ETRS606` dans STM32CubeIDE, configurer l'IP du VPS et la clé API dans `app_netxduo.c`, build, flash. La bannière UART confirme le démarrage.

### Backend VPS

```bash
pip install flask tensorflow joblib scikit-learn numpy gunicorn
export METEO_API_KEY="votre_cle"
gunicorn -w 2 -b 0.0.0.0:5000 app:app
```

### Dashboard

Fichiers statiques servis directement par Caddy depuis `/usr/share/caddy/atmosai`. Modifier `API_BASE` dans `index.html` si besoin.

---

## Équipe

| | |
|---|---|
| **William Z.** | The Sovereign Lord of the TRI |
| **Franck G.** | The Stoic Sentinel of the CPU Debugger |
| **Mostapha K.** | The Holy Exorcist of the ESET UART Console |

---

*ETRS606 — IA Embarquée · Université Savoie Mont Blanc · 2026*
