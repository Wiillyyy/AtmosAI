# AtmosAI — Embedded Weather Station with Edge & Cloud AI

> **Module ETRS606 — Embedded AI · Université Savoie Mont Blanc**  
> William Z. · Franck G. · Mostapha K.

---

## What we built

We designed a complete end-to-end embedded weather station — from the physical sensor all the way to a website hosted on our own server, including a neural network running **directly on our microcontroller**, no cloud, no internet, in under a millisecond.

Our STM32 board reads temperature, humidity and pressure every 20 seconds. It computes 13 features in real time, runs a full MLP inference locally, and sends everything — measurements + prediction — to our Flask API hosted on a VPS. The website displays it all live, auto-refreshes, and we can even send commands to the board from the browser.

We didn't use ThingSpeak. We didn't use MATLAB. We did everything ourselves.

---

## Architecture at a glance

```
┌──────────────────────────────────────────────────────┐
│                   STM32N657X0 BOARD                  │
│                                                      │
│  HTS221 (temp/hum) + LPS22HH (pressure)  ← I2C      │
│              ↓                                       │
│   Ring buffer 560 samples (~3h)                      │
│              ↓                                       │
│   13 features computed in real time                  │
│              ↓                                       │
│   h1_infer() — MLP C float32 — < 1ms                 │
│   Output: Clear / Rain / Snow + confidence           │
│              ↓                                       │
│   HTTP POST every ~20s via Ethernet                  │
└──────────────────────┬───────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│              VPS — Flask API + SQLite                │
│                                                      │
│   SQLite storage → Keras inference J+1/J+2/J+3      │
│   Public routes + admin routes protected by API key  │
└──────────────────────┬───────────────────────────────┘
                       ↓
┌──────────────────────────────────────────────────────┐
│            Web Dashboard — index.html                │
│                                                      │
│   Live metrics · Sparklines · Predictions            │
│   Live STM32 features · History · Comparison         │
└──────────────────────────────────────────────────────┘
```

---

## Hardware

We use a **NUCLEO-N657X0** board (STM32N657X0 — Cortex-M55 at 800 MHz) with the **X-NUCLEO-IKS01A3** shield. Sensors communicate over I2C :
- **HTS221** for temperature and humidity (±0.5°C / ±3.5% RH)
- **LPS22HH** for atmospheric pressure (±0.1 hPa)

The board runs **Azure RTOS ThreadX** with two independent threads : a sensor thread that reads, computes and infers, and a TCP thread that posts results to the VPS.

---

## The embedded model — H+1

This is the part we're most proud of.

We trained an MLP in Python/TensorFlow on **43,821 hours** of historical weather data from Aix-les-Bains (Open-Meteo, 2019–2023). We exported the weights to C float32 via X-CUBE-AI. The network runs entirely on the board — no network dependency, no cloud latency, no NPU.

```
Input (13 features)
      ↓
Dense(32, ReLU)   ← BN fused into weights
      ↓
Dense(32, ReLU)
      ↓
Dense(16, ReLU)
      ↓
Dense(3, Softmax)
      ↓
Clear · Rain · Snow
```

**87.5% accuracy. ~8 KB. Inference in < 1 ms. CPU load < 1%.**

### The 13 features

What makes our model robust is that it doesn't just look at raw values. It receives **dynamically computed features** from a ring buffer of 560 samples (~3h of history) kept in RAM :

| # | Feature | Source |
|---|---|---|
| 1–3 | temp, hum, pres | Direct sensors |
| 4–5 | ΔT 1h, ΔT 3h | Ring buffer |
| 6–7 | ΔP 1h, ΔP 3h | Ring buffer |
| 8 | ΔH 1h | Ring buffer |
| 9–10 | hour_sin, hour_cos | RTC (cyclic encoding) |
| 11–12 | month_sin, month_cos | RTC (cyclic encoding) |
| 13 | T − dew point | Embedded Magnus formula |

Deltas capture the **trend** (pressure drop → likely rain). The sin/cos encoding of hour and month avoids the "11pm is far from midnight" artefact — the model knows time is continuous. The dew point is directly predictive of condensation and precipitation.

### Why the NPU is not used

We tried. X-CUBE-AI generated the code with the LL_ATON runtime. On the first call, BusFault — immediate crash. After analysing the CFSR register and the faulting address, we identified the cause : the ATON input buffer points to `0x342e0000` (AXISRAM5/npuRAM5), a region wired **exclusively to the NPU AXI bus**. The CPU has no access path to this memory.

Anyway, X-CUBE-AI had compiled all blocks as `EpochBlock_Flags_pure_sw` — the hardware NPU wasn't involved at all, our model is too small to justify it. Our `h1_infer()` does exactly the same computations, same weights, same results — but in CPU-accessible SRAM.

---

## The VPS backend

We refused to depend on ThingSpeak. We deployed our own stack :

- **Flask** (Python) served by **Gunicorn**
- **SQLite** for measurement storage
- **Caddy** to serve static files
- **systemd** for resilience (automatic restart)

Our API exposes public routes (data reading, forecasts) and admin routes protected by key (`purge, clear database, send command to board`).

### Cloud models J+1 / J+2 / J+3

Alongside the embedded H+1, we trained **3 independent Keras models** on the VPS for longer-range forecasts, using the same 13 features (recomputed from the SQLite history) :

| Model | Horizon | Balanced Accuracy |
|---|---|---|
| J+1 | +24h | 64.3% |
| J+2 | +48h | 61.2% |
| J+3 | +72h | 54.4% |

The degradation with horizon is expected — predicting at 72h with only local sensors and no NWP data is a fundamentally hard problem.

---

## The web dashboard

A vanilla HTML/CSS/JS file, no framework. Switchable dark/light theme. Auto-refresh every 15 seconds.

**What we display on the dashboard :**
- Live values in large (temperature cyan, humidity amber, pressure green)
- H+1 prediction from the board : class + animated confidence bar + floating emoji
- **Live STM32 features** : 10 values computed in JS from history (deltas, dew point, cyclical) — colour-coded green/red by sign, exactly the same formulas as in the embedded C
- 3 Canvas 2D sparklines (bézier curves with gradient fill)
- Latest measurements table
- History tab with interactive Chart.js charts
- AI Model tab with full technical details + Edge vs Cloud comparison table

**Admin page** (`admin.html`) : database management, purge, downlink commands to the board (including a strobe light mode for the demo).

---

## Embedded performance measurement

We measure CPU load and estimated power consumption in real time using the **DWT counter** (Data Watchpoint and Trace) of the Cortex-M55 — a hardware cycle counter accurate to the nanosecond. Every cycle, the UART prints :

```
==========================================
[PWR] Cycle period  :  20043 ms
[PWR] CPU load      :  0.3 %
[PWR] h1_infer()    :  0.18 us  (144 cyc)
[PWR] Est. current  :  30.4 mA
[PWR] Est. power    :  100 mW  (0.100 W)
==========================================
```

0.3% CPU load to run the AI. The board sleeps 99.7% of the time.

---

## Edge AI vs Cloud AI

| Criterion | STM32 H+1 — Edge | VPS J+1/J+2/J+3 — Cloud |
|---|---|---|
| Horizon | H+1 | J+1 / J+2 / J+3 |
| Accuracy | **87.5%** | 80.9% (J+1 avg.) |
| Model size | **~8 KB** | ~120 KB (Keras) |
| Inference latency | **< 1 ms** (local) | ~50 ms (network) |
| Network dependency | **None** | Requires connection |
| Power consumption | **~100 mW** | Server 24/7 |
| Update | Reflashing | Flask restart |

Our **Edge-first** approach is consistent with digital sobriety : critical inference happens locally, the network is only used for archiving and enriching — not for running the main model.

---

## Key numbers

| Metric | Value |
|---|---|
| H+1 embedded accuracy | **87.5%** |
| Embedded model size | **~8 KB** |
| STM32 inference time | **< 1 ms** |
| AI CPU load | **< 1%** |
| Training observations | **43,821** |
| Features | **13** |
| Measurement cycle | **~20 s** |
| Cloud models | **3** (J+1 · J+2 · J+3) |

---

## Running the project

### Firmware

Open `AtmosAI_ETRS606` in STM32CubeIDE, set the VPS IP and API key in `app_netxduo.c`, build, flash. The UART banner confirms startup.

### VPS backend

```bash
pip install flask tensorflow joblib scikit-learn numpy gunicorn
export METEO_API_KEY="your_key"
gunicorn -w 2 -b 0.0.0.0:5000 app:app
```

### Dashboard

Static files served directly by Caddy from `/usr/share/caddy/atmosai`. Update `API_BASE` in `index.html` if needed.

---

## Team

| | |
|---|---|
| **William Z.** | The Sovereign Lord of the TRI |
| **Franck G.** | The Stoic Sentinel of the CPU Debugger |
| **Mostapha K.** | The Holy Exorcist of the ESET UART Console |

---

*ETRS606 — Embedded AI · Université Savoie Mont Blanc · 2026*
