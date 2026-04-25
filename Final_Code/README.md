# AtmosAI — Station météo STM32 + IA embarquée + VPS

**NUCLEO-N657X0 · X-NUCLEO-IKS01A3 · Azure RTOS / ThreadX · NetXDuo · X-CUBE-AI / STAI · Flask VPS**

> Projet ETRS606 — William Z. · Franck G. · Mostapha K.

---

## Objectif du projet

AtmosAI est une station météo embarquée construite autour d'une carte **NUCLEO-N657X0** et d'un shield **X-NUCLEO-IKS01A3**.

Le système réalise toute la chaîne suivante :

1. Acquisition locale des capteurs météo et inertiels en I2C.
2. Calcul d'une prédiction météo **H+1** directement sur la carte.
3. Mesure de performance embarquée via le compteur DWT du Cortex-M55.
4. Envoi des mesures vers un VPS par HTTP `POST`.
5. Récupération de commandes depuis le VPS par HTTP `GET`.
6. Affichage web temps réel : dashboard principal, page carte détaillée, page admin.

La version finale privilégie la stabilité de démonstration : l'initialisation NPU/STAI est présente et documentée, mais l'appel effectif `stai_network_run()` est désactivé à cause d'une erreur bus bas niveau de l'Epoch Controller. L'inférence H+1 stable est donc exécutée en fallback CPU.

---

## Matériel utilisé

| Élément | Rôle |
|---|---|
| **NUCLEO-N657X0** | Carte STM32N657X0, Cortex-M55, bloc NPU ATON intégré |
| **X-NUCLEO-IKS01A3** | Shield capteurs I2C |
| **HTS221** | Température + humidité |
| **LPS22HH** | Pression atmosphérique |
| **LSM6DSO** | Accéléromètre + gyroscope |
| **LAN8742** | PHY Ethernet utilisé par NetXDuo |

---

## Vue d'architecture

```text
STM32 NUCLEO-N657X0
│
├─ ThreadX / Azure RTOS
│  │
│  ├─ Thread capteurs
│  │  ├─ Lecture HTS221 : température / humidité
│  │  ├─ Lecture LPS22HH : pression
│  │  ├─ Lecture LSM6DSO : accélération / gyroscope
│  │  ├─ h1_push() : insertion T/RH/P dans le ring buffer
│  │  ├─ h1_infer() : prédiction H+1 CPU
│  │  └─ LEDs + mesures DWT / CPU load / puissance estimée
│  │
│  └─ Thread TCP NetXDuo
│     ├─ Résolution DNS manuelle UDP vers 8.8.8.8
│     ├─ Fallback automatique sur IP fixe si DNS indisponible
│     ├─ POST /api/data vers Flask
│     └─ GET  /api/command depuis Flask
│
└─ VPS
   ├─ Flask API
   ├─ SQLite
   ├─ index.html  : dashboard principal
   ├─ index2.html : télémétrie carte + visualisation 3D
   └─ admin.html  : commandes serveur -> carte
```

---

## Fichiers importants

| Fichier | Rôle |
|---|---|
| `FSBL/Core/Src/main.c` | Initialisation HAL, clocks, UART, Ethernet, I2C, cache, isolation, horloge NPU, démarrage ThreadX |
| `FSBL/NetXDuo/App/app_netxduo.c` | Capteurs, IMU, NetXDuo, DHCP, DNS, POST, GET, mode danse, init runtime NPU |
| `FSBL/X-CUBE-AI/App/h1_inference.c` | Ring buffer, features, tentative init STAI, fallback MLP CPU |
| `FSBL/X-CUBE-AI/App/h1_weights.h` | Poids float32 du modèle H+1 CPU |
| `FSBL/X-CUBE-AI/App/stai_network.c/.h` | API STAI générée par X-CUBE-AI |
| `FSBL/X-CUBE-AI/App/network.c` | Graphe NPU généré |
| `FSBL/X-CUBE-AI/App/network_weights_blob.c` | Poids / blob réseau côté NPU |
| `STM32CubeIDE/FSBL/makefile.defs` | Ajout manuel du runtime LL_ATON et des fichiers X-CUBE-AI au build |

---

## Acquisition capteurs

Le thread capteurs initialise trois contextes I2C STMEMS :

- `dev_ctx_hts221` pour température / humidité.
- `dev_ctx_lps22hh` pour pression.
- `dev_ctx_lsm6dso` pour accéléromètre / gyroscope.

Le LSM6DSO est détecté en testant les deux adresses possibles du shield. Si l'ID capteur correspond, la carte publie :

```text
imu_ok
accel_x_mg, accel_y_mg, accel_z_mg
gyro_x_mdps, gyro_y_mdps, gyro_z_mdps
```

Ces valeurs sont envoyées au VPS et utilisées dans `index2.html` pour animer la visualisation 3D de la carte.

---

## Modèle H+1 embarqué

Le modèle H+1 est un MLP entraîné en Python puis exporté en C sous forme de tableaux de poids.

```text
Input 13
  -> Dense 32 + ReLU
  -> Dense 32 + ReLU
  -> Dense 16 + ReLU
  -> Dense 3  + Softmax
```

Classes de sortie :

| Index | Classe |
|---|---|
| 0 | Clair |
| 1 | Pluie |
| 2 | Brouillard |

Accuracy actuelle du modèle : **87.5 %**.

### Features d'entrée

| # | Feature | Origine |
|---|---|---|
| 1 | Température | HTS221 |
| 2 | Humidité relative | HTS221 |
| 3 | Pression | LPS22HH |
| 4 | Delta température 1h | Ring buffer |
| 5 | Delta température 3h | Ring buffer |
| 6 | Delta pression 1h | Ring buffer |
| 7 | Delta pression 3h | Ring buffer |
| 8 | Delta humidité 1h | Ring buffer |
| 9 | `sin(heure)` | Temps VPS |
| 10 | `cos(heure)` | Temps VPS |
| 11 | `sin(mois)` | Temps VPS |
| 12 | `cos(mois)` | Temps VPS |
| 13 | Température - point de rosée | Formule de Magnus embarquée |

Le modèle utilise donc à la fois l'état instantané, les tendances météo, le cycle jour/nuit, la saison et l'écart au point de rosée.

### Ring buffer

Dans `h1_inference.c`, les mesures sont stockées dans un ring buffer :

```c
#define H1_BUF_LEN 560
```

Chaque entrée contient :

```c
temp, rhum, pres, tick_ms
```

Le code cherche ensuite la mesure la plus proche de `now - 1h` ou `now - 3h` avec une tolérance de ±5 minutes. Si l'historique n'est pas encore suffisant, le firmware l'indique en console et utilise des deltas à zéro.

Note technique : le projet est aujourd'hui cadencé à environ **5 s** par cycle pour la démo live. Avec un cycle de 5 s, 3h d'historique correspondent à `10800 / 5 = 2160` échantillons. La version actuelle conserve le comportement de fallback si les deltas longs sont indisponibles.

---

## Intégration NPU / STAI

Cette section décrit ce qui a été réellement tenté sur le NPU STM32N6.

### But initial

L'objectif initial était d'exécuter le réseau généré par X-CUBE-AI via le runtime STAI / LL_ATON, donc d'utiliser le NPU ATON du STM32N657X0 pour l'inférence météo.

### Éléments intégrés au projet

Le build contient les sources runtime et réseau suivantes :

```text
Middlewares/ST/AI/Npu/ll_aton/...
Middlewares/ST/AI/Npu/Devices/STM32N6XX/...
FSBL/X-CUBE-AI/App/network.c
FSBL/X-CUBE-AI/App/stai_network.c
FSBL/X-CUBE-AI/App/network_weights_blob.c
```

Le fichier `STM32CubeIDE/FSBL/makefile.defs` ajoute manuellement ces objets au build :

```makefile
-DLL_ATON_PLATFORM=9
-DLL_ATON_OSAL=4
-DLL_ATON_SW_FALLBACK=1
-DNDEBUG
```

`LL_ATON_PLATFORM=9` correspond à la plateforme STM32N6. `LL_ATON_OSAL=4` sélectionne l'intégration ThreadX.

### Initialisation qui fonctionne

Dans `main.c`, l'horloge NPU est activée :

```c
__HAL_RCC_NPU_CLK_ENABLE();
```

Dans `app_netxduo.c`, la fonction `App_Try_Init_Npu()` appelle :

```c
npu_risaf_init();
LL_ATON_RT_RuntimeInit();
```

Dans `h1_inference.c`, le contexte STAI est créé puis initialisé :

```c
stai_network_init((stai_network *)s_stai_ctx);
```

La console confirme cette partie :

```text
[RIF] RISAF4+5 configures (NPU_MST0+1 -> AXISRAM2/3/5)
[NPU] Initialisation runtime...
[NPU] Runtime initialise
[H1] NPU: reseau initialise (STAI OK)
```

Donc le runtime ATON démarre et le réseau STAI s'initialise.

### Erreur rencontrée au lancement NPU

Le problème apparaît au moment de lancer réellement :

```c
stai_network_run(...)
```

L'Epoch Controller remonte immédiatement :

```text
Epoch Controller ERROR interrupt: EC_IRQ = 0x00000008
Epoch Controller opcode counter: 0x00000000
Epoch Controller label: 0x00000000
```

Le point important est `opcode counter = 0`. Cela indique que le NPU échoue avant même d'exécuter la première opération utile. L'hypothèse la plus cohérente est une erreur d'accès mémoire côté bus AXI : le NPU ne lit pas correctement le blob EC, les poids ou les buffers.

### Pistes testées

Plusieurs corrections ont été testées :

| Piste | Objectif | Résultat |
|---|---|---|
| Déplacement des poids hors OCTOFLASH | Éviter l'écrasement / accès externe instable | Build plus propre, mais EC_IRQ persistant |
| Placement en AXISRAM | Rendre les poids accessibles plus tôt au boot | EC_IRQ persistant |
| Désactivation assertions LL_ATON avec `-DNDEBUG` | Éviter crash immédiat du runtime sur erreur | Utile pour stabilité build/runtime, mais ne corrige pas l'accès NPU |
| Configuration RISAF4 / RISAF5 | Autoriser les deux masters NPU sur les zones AXISRAM utilisées | Registres écrits, GLOCK non bloquant, mais EC_IRQ persistant |
| Tentative CID0 + CID1 | Couvrir les CIDs supposés CPU/NPU | EC_IRQ persistant |
| Fallback CPU | Conserver une démonstration stable | Fonctionnel |

### Configuration RISAF testée

D'après la documentation STM32N6, les deux masters NPU sont associés aux régions RISAF suivantes :

| Instance | Rôle | Base secure |
|---|---|---|
| RISAF4 | NPU_MST0 | `0x54029000` |
| RISAF5 | NPU_MST1 | `0x5402A000` |

Dans `npu_risaf_init()`, deux zones sont ouvertes sur RISAF4 et RISAF5 :

| Région | Adresse | Usage supposé |
|---|---|---|
| Région 1 | `0x34100000 - 0x342DFFFF` | Blob EC + poids / AXISRAM2-3 |
| Région 2 | `0x342E0000 - 0x3434FFFF` | Buffers I/O NPU / AXISRAM5 |

La configuration autorise CID0 et CID1 en lecture/écriture. Les registres s'écrivent bien, ce qui indique que le bloc RISAF n'est pas verrouillé par `GLOCK`. Malgré cela, l'erreur EC reste identique.

### Conclusion NPU

Le blocage est probablement situé sous le niveau applicatif : configuration RIF/RISAF/RIMC exacte, CID effectif des masters NPU, ou mapping mémoire généré par X-CUBE-AI.

La solution la plus propre serait de repartir d'un projet CubeMX/X-CUBE-AI généré spécifiquement pour NPU STM32N6, puis de réintégrer notre logique capteurs/réseau. Ce projet évite volontairement cette régénération complète afin de ne pas casser la partie fonctionnelle : Ethernet, ThreadX, capteurs, POST/GET, dashboard et démonstration.

État final assumé :

- NPU clock : OK
- Runtime LL_ATON : OK
- `stai_network_init()` : OK
- `stai_network_run()` : désactivé car EC_IRQ fatal
- Inférence finale : MLP CPU stable

---

## Réseau embarqué

Le réseau utilise NetXDuo en client TCP.

### DHCP et passerelle

Le thread NetXDuo attend une adresse IP via DHCP, récupère la passerelle, puis démarre le thread TCP applicatif.

### DNS avec fallback IP

Le projet n'utilise pas l'addon `nxd_dns`, absent du projet généré. Une résolution DNS minimale a donc été ajoutée en UDP brut :

```text
DNS server : 8.8.8.8
Host       : atmosai.willydev.xyz
Type       : A record
```

Si la résolution échoue, le firmware conserve l'adresse IP fixe du VPS.

Cela permet d'avoir un fonctionnement propre quand le DNS marche, tout en gardant une démonstration robuste sur un réseau inconnu.

### POST STM32 -> VPS

Après chaque cycle capteurs + inférence, `g_sample_seq` est incrémenté. Le thread TCP détecte ce changement et envoie une seule fois la dernière mesure complète :

```http
POST /api/data HTTP/1.1
Host: atmosai.willydev.xyz
Content-Type: application/json
X-API-Key: atmosai_w1lly_2026
Connection: close
```

Le JSON contient notamment :

```json
{
  "device_id": "NUCLEO-N657X0",
  "temperature": 23.4,
  "humidity": 52.1,
  "pressure": 995.6,
  "prediction_h1": "Clair",
  "confidence_h1": 0.87,
  "imu_ok": 1,
  "accel_x_mg": 0.0,
  "accel_y_mg": 0.0,
  "accel_z_mg": 1000.0,
  "gyro_x_mdps": 0.0,
  "gyro_y_mdps": 0.0,
  "gyro_z_mdps": 0.0,
  "cpu_load": 0.9,
  "infer_time_us": 26500.0,
  "power_mw": 103.0,
  "cycle_ms": 5060,
  "uptime_s": 123,
  "post_status": "ok",
  "post_ok_count": 12,
  "post_fail_count": 0
}
```

### GET VPS -> STM32

Après le POST, la carte interroge le serveur :

```http
GET /api/command HTTP/1.1
Host: atmosai.willydev.xyz
X-API-Key: atmosai_w1lly_2026
Connection: close
```

La réponse contient une commande éventuelle, par exemple :

```json
{"cmd":"dance","hour":7,"month":4}
```

La commande est stockée dans `g_server_cmd`, puis exécutée par le thread capteurs au cycle suivant.

---

## Mode danse

Le mode danse sert à démontrer la communication **serveur -> carte**.

Déroulement :

1. L'utilisateur clique sur le bouton admin dans `admin.html`.
2. Flask stocke la commande `dance`.
3. La carte récupère la commande via `GET /api/command`.
4. Le thread capteurs met en pause le cycle normal.
5. Les LEDs clignotent pendant environ **20 s**.
6. Une barre de progression est imprimée sur l'UART.
7. Les capteurs et l'inférence reprennent ensuite normalement.

---

## VPS et dashboard

Le VPS héberge :

- une API Flask ;
- une base SQLite ;
- `index.html` : dashboard principal ;
- `index2.html` : page carte détaillée ;
- `admin.html` : commandes et maintenance.

Les données capteurs et télémétries sont stockées en base, y compris les valeurs IMU, le CPU load, le temps d'inférence, la puissance estimée et l'état des POST. Cela évite de dépendre uniquement d'un état mémoire Flask.

Fonctions côté web :

- thème clair/sombre persistant ;
- affichage live météo ;
- prédiction H+1 ;
- historique Chart.js ;
- axe séparé pour la pression ;
- télémétrie CPU / réseau / inférence ;
- affichage IMU ;
- visualisation pseudo-3D de la carte ;
- LEDs 3D colorées selon la prédiction ;
- page admin avec downlink commande.

---

## Mesure de performance embarquée

Le firmware utilise le compteur matériel DWT du Cortex-M55 pour mesurer :

- la période réelle du cycle ;
- le temps passé dans `h1_infer()` ;
- le CPU load estimé ;
- le courant et la puissance estimés.

Exemple de console :

```text
[PWR] Periode cycle : 5060 ms
[PWR] CPU load      : 0.9 %
[PWR] h1_infer()    : 26513.47 us  (15908083 cyc)
[PWR] I estimee     : 31.1 mA
[PWR] P estimee     : 103 mW  (0.103 W)
```

---

## Build

Le projet est ouvert et compilé avec STM32CubeIDE.

Points particuliers :

- Le FSBL tourne en secure world avec TrustZone.
- Les objets LL_ATON / STAI sont ajoutés manuellement via `makefile.defs`.
- Le code NetXDuo et les capteurs sont dans `FSBL/NetXDuo/App/app_netxduo.c`.
- Le modèle H+1 CPU et l'init STAI sont dans `FSBL/X-CUBE-AI/App/h1_inference.c`.

Le projet a volontairement évité une régénération CubeMX complète en fin de développement, car cela risquait de casser la pile déjà fonctionnelle : Ethernet, ThreadX, capteurs, API et dashboard.

---

## État final du projet

Fonctionnel :

- acquisition HTS221 / LPS22HH ;
- acquisition LSM6DSO ;
- prédiction H+1 CPU ;
- mesure DWT ;
- POST vers VPS ;
- GET commande depuis VPS ;
- DNS avec fallback IP ;
- mode danse ;
- dashboard principal ;
- dashboard carte détaillée ;
- page admin.

Partiellement intégré :

- runtime NPU LL_ATON ;
- init réseau STAI ;
- configuration RISAF expérimentale.

Non activé en version finale :

- `stai_network_run()` sur NPU, à cause de `EC_IRQ = 0x00000008`.

---

## Équipe

| Membre | Contribution principale |
|---|---|
| William Z. | TRI |
| Franck G. | TRI |
| Mostapha K. | ESET |


## Sources

### Documentation STM32 / carte

- **RM0486 — STM32N647/657xx Reference Manual**  
  https://www.st.com/resource/en/reference_manual/dm00769900.pdf

- **STM32N647xx / STM32N657xx Datasheet — DS14791**  
  https://www.st.com/resource/en/datasheet/stm32n657l0.pdf

- **NUCLEO-N657X0-Q — page produit ST**  
  https://www.st.com/en/evaluation-tools/nucleo-n657x0-q.html

- **UM3417 — STM32N6 Nucleo-144 board MB1940 User Manual**  
  https://www.st.com/resource/en/user_manual/um3417-stm32n6-nucleo144-board-mb1940-stmicroelectronics.pdf

### Shield capteurs

- **X-NUCLEO-IKS01A3 — page produit ST**  
  https://www.st.com/en/evaluation-tools/x-nucleo-iks01a3.html

- **UM2559 — X-NUCLEO-IKS01A3 User Manual**  
  https://www.st.com/resource/en/user_manual/um2559-getting-started-with-the-xnucleoiks01a3-motion-mems-and-environmental-sensor-expansion-board-for-stm32-nucleo-stmicroelectronics.pdf

### Datasheets capteurs

- **HTS221 — humidity and temperature sensor**  
  https://www.st.com/en/mems-and-sensors/hts221.html

- **LPS22HH — pressure sensor**  
  https://www.st.com/en/mems-and-sensors/lps22hh.html

- **LSM6DSO — 6-axis accelerometer + gyroscope IMU**  
  https://www.st.com/en/mems-and-sensors/lsm6dso.html

### Middleware / IA embarquée

- **STM32CubeN6 Firmware Package**  
  https://www.st.com/en/embedded-software/stm32cuben6.html

- **X-CUBE-AI — AI expansion package for STM32CubeMX**  
  https://www.st.com/en/embedded-software/x-cube-ai.html

- **ST Edge AI Suite / STM32Cube.AI documentation**  
  https://www.st.com/content/st_com/en/ecosystems/st-edge-ai-suite.html

- **Azure RTOS ThreadX documentation**  
  https://github.com/eclipse-threadx/threadx

- **Azure RTOS NetXDuo documentation**  
  https://github.com/eclipse-threadx/netxduo

### Données météo / backend / dashboard

---

*ETRS606 — IA embarquée · Université Savoie Mont Blanc · 2026*

