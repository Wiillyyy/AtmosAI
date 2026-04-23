# AtmosAI — Station météo embarquée ETRS606

**STM32N657X0 · Azure RTOS (ThreadX + NetXDuo) · MLP H+1 · STAI NPU**

> Projet étudiant ETRS606 — William Z., Franck G., Mostapha K.

---

## Vue d'ensemble

Station météo embarquée sur carte STM32N657X0-DK (Cortex-M55, 600 MHz, NPU ATON intégré).  
Mesure température, humidité et pression toutes les ~5 s, prédit la météo à H+1 (Clair / Pluie / Brouillard) via un réseau de neurones MLP embarqué, et remonte les données vers un serveur TCP distant.

---

## Matériel

| Composant | Rôle |
|-----------|------|
| STM32N657X0-DK | MCU principal — Cortex-M55 + NPU ATON |
| HTS221 | Capteur température + humidité (I2C) |
| LPS22HH | Capteur pression barométrique (I2C) |
| LAN8742 | PHY Ethernet (RMII) |

---

## Architecture logicielle

```
ThreadX (RTOS sécurisé TrustZone)
├── Task capteurs     — acquisition HTS221 + LPS22HH toutes les ~5 s
├── Task NetXDuo      — DHCP + TCP client (envoi données serveur)
└── Task inference    — prédiction météo H+1 (MLP embarqué)

FSBL (First Stage Boot Loader) — secure world
├── Core/              — init HAL, clocks, MPU, CACHEAXI
├── NetXDuo/App/       — stack réseau, thread TCP, init NPU
└── X-CUBE-AI/App/     — inference H+1 (h1_inference.c + h1_weights.h)
```

---

## Modèle de prédiction H+1

### Features (13 entrées)

| # | Feature | Description |
|---|---------|-------------|
| 0 | `temp` | Température courante (°C) |
| 1 | `rhum` | Humidité relative (%) |
| 2 | `pres` | Pression atmosphérique (hPa) |
| 3 | `dT_1h` | Delta température sur 1h |
| 4 | `dT_3h` | Delta température sur 3h |
| 5 | `dP_1h` | Delta pression sur 1h |
| 6 | `dP_3h` | Delta pression sur 3h |
| 7 | `dRH_1h` | Delta humidité sur 1h |
| 8-9 | `sin/cos(heure)` | Encodage cyclique heure du jour |
| 10-11 | `sin/cos(mois)` | Encodage cyclique mois |
| 12 | `temp - Td` | Écart température / point de rosée (Magnus) |

### Architecture MLP (CPU)

```
Input (13) → Dense(32, ReLU) → Dense(32, ReLU) → Dense(16, ReLU) → Dense(3, Softmax)
```

Classes de sortie : **Clair** (0) · **Pluie** (1) · **Brouillard** (2)

Les poids sont extraits du modèle entraîné (`extract_weights.py`) et compilés statiquement dans `h1_weights.h`.

### Ring buffer temporel

2240 échantillons × ~5 s ≈ 3h06min d'historique — nécessaire pour calculer les deltas à 1h et 3h.  
Les deltas sont indisponibles au démarrage (affichage du temps restant en console).

---

## Intégration NPU — journal technique

C'est la partie la plus complexe du projet. Voici ce qui a été tenté, dans l'ordre, et pourquoi ça bloque.

### Contexte hardware

Le STM32N657X0 embarque un NPU ATON (ST proprietary) avec :
- Deux ports maîtres AXI (NPU_MST0, NPU_MST1)
- Ses propres banks SRAM internes (AXISRAM4/5/6, alias npuRAM)
- Un Epoch Controller (EC) qui exécute des blobs de microinstructions

Le NPU est piloté via la STAI API (ST AI runtime) générée par X-CUBE-AI.

### Ce qui fonctionne

```
LL_ATON_RT_RuntimeInit()   ✓  — hardware NPU initialisé, OSAL ThreadX OK
stai_network_init()        ✓  — instance réseau configurée (STAI_SUCCESS)
```

En console :
```
[RIF] RISAF4+5 configures (NPU_MST0+1 -> AXISRAM2/3/5)
[NPU] Initialisation runtime...
[NPU] Runtime initialise
[H1] NPU: reseau initialise (STAI OK)
```

### Le blocage : EC_IRQ = 0x00000008

À chaque appel de `stai_network_run()`, l'Epoch Controller lève immédiatement une interruption d'erreur :

```
Epoch Controller ERROR interrupt: EC_IRQ = 0x00000008
Epoch Controller opcode counter: 0x00000000
Epoch Controller label: 0x00000000
```

**opcode=0, label=0** : le NPU échoue avant même sa première instruction utile.  
C'est une erreur de bus AXI — le NPU ne peut pas lire le blob EC ou les poids réseau.

### Analyse RIF (Resource Isolation Framework)

D'après RM0486, le STM32N6 utilise un framework de sécurité mémoire :

- **RIMC** (`RIFSC_RIMC_ATTR`) : assigne un CID (Context ID) à chaque maître AXI
- **RISAF** : filtre les accès mémoire par CID, côté initiateur ou cible

Instances RISAF identifiées :

| Instance | Maître | Base (secure) |
|----------|--------|---------------|
| RISAF4 | NPU_MST0 | 0x54029000 |
| RISAF5 | NPU_MST1 | 0x5402A000 |

**Configuration tentée** (dans `App_Try_Init_Npu`, avant `LL_ATON_RT_RuntimeInit`) :

```c
// Régions RISAF4 + RISAF5 pour les deux masters NPU :
// Région 1 : 0x34100000–0x342DFFFF (EC blob + poids, AXISRAM2/3)
// Région 2 : 0x342E0000–0x3434FFFF (buffers I/O, AXISRAM5)
// CID0 + CID1 autorisés en lecture + écriture, mode secure
```

Les registres s'écrivent sans erreur (GLOCK = 0), mais l'EC_IRQ persiste.

### Conclusion NPU

Le CID effectif des maîtres NPU au moment du `stai_network_run()` est non identifiable sans debug JTAG niveau registres (lecture de `RIMC_ATTR` au runtime). Il est probable que :

- le boot ROM assigne un CID ≠ 0 et ≠ 1 aux ports NPU, ou
- la configuration RISAF nécessite un paramètre supplémentaire (CFEN, sous-régions A/B), ou
- il existe une autre couche de filtrage non documentée dans la version de RM disponible.

**La voie propre** serait de repartir d'un projet CubeMX avec X-CUBE-AI NPU coché — CubeMX génère automatiquement la configuration RISAF correcte pour le NPU, le bon linker script, et les bonnes zones mémoire.

### État final

`stai_network_init()` est appelé et réussit à chaque cycle. `stai_network_run()` n'est pas appelé (EC_IRQ fatal). La prédiction est assurée par le MLP CPU. Le NPU est visible dans le code et la console au niveau initialisation.

---

## Build

### Prérequis

- STM32CubeIDE 2.1.1
- Toolchain GNU arm-none-eabi 14.3
- Azure RTOS (ThreadX + NetXDuo) — inclus dans le repo

### Compilation

```bash
# Dans STM32CubeIDE : Project > Build All
# Les règles custom (LL_ATON runtime, h1_inference) sont dans :
STM32CubeIDE/FSBL/makefile.defs
STM32CubeIDE/FSBL/makefile.targets
```

Flags importants dans `makefile.defs` :

```makefile
-DLL_ATON_PLATFORM=9   # STM32N6 (pas 12 = STM32H7P !)
-DLL_ATON_OSAL=4       # ThreadX
-DLL_ATON_SW_FALLBACK=1
-DNDEBUG               # désactive LL_ATON_ASSERT (évite crash sur EC_IRQ)
```

### Linker script

`STM32CubeIDE/FSBL/STM32N657X0HXQ_AXISRAM2_fsbl.ld`

- ROM : 0x34180400 (255 Ko) — code + poids réseau (`.npu_weights`)
- RAM : 0x341C0000 (256 Ko) — données + stack + heap

---

## Sorties console (exemple)

```
[RIF] RISAF4+5 configures (NPU_MST0+1 -> AXISRAM2/3/5)
[NPU] Initialisation runtime...
[NPU] Runtime initialise

[CYCLE 1] ==================================
[SNS] Pression    : 992.24 hPa
[SNS] Temperature : 24.72 C
[SNS] Humidite    : 57.84 %
[H1] NPU: reseau initialise (STAI OK)
[H1] Capteurs: T=24.7 RH=57.8 P=992.2
[H1] Deltas:   dT1h=+0.00 dT3h=+0.00 dP1h=+0.00 dP3h=+0.00 dRH1h=+0.00
[H1] Sortie: Clair=0.682 Pluie=0.311 Brouillard=0.008 -> Clair
[H1] Resume: Clair (68.2%)  scores: C=0.68 P=0.31 B=0.01
[PWR] Periode cycle : 5050 ms
[PWR] CPU load      : 1.1 %
[PWR] h1_infer()    : 30000 us
```

---

## Structure du repo

```
FSBL/
├── Core/Src/main.c              — init système, clocks, MPU
├── NetXDuo/App/app_netxduo.c   — DHCP, TCP, init NPU + RISAF
└── X-CUBE-AI/App/
    ├── h1_inference.c           — inference H+1 (MLP CPU + init NPU)
    ├── h1_weights.h             — poids MLP (généré par extract_weights.py)
    ├── network.c                — graphe réseau NPU (généré X-CUBE-AI)
    ├── network_ecblobs.h        — blobs Epoch Controller NPU
    ├── network_weights_blob.c   — poids quantifiés NPU (int8)
    └── stai_network.h/c         — API STAI générée

STM32CubeIDE/FSBL/
├── makefile.defs                — règles build custom (LL_ATON + h1_inference)
├── makefile.targets             — sources NetXDuo / HAL restaurées
└── STM32N657X0HXQ_AXISRAM2_fsbl.ld  — linker script
```

---

## Fichiers clés à lire en premier

| Fichier | Pourquoi |
|---------|----------|
| `h1_inference.c` | Cœur du projet — ring buffer, features, MLP, init NPU |
| `app_netxduo.c` | `npu_risaf_init()` + `App_Try_Init_Npu()` |
| `makefile.defs` | Tout le build custom LL_ATON |
| `network_ecblobs.h` | Blob EC NPU généré — 568 instructions ATON |
