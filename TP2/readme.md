# TP2 — Interface Capteur & STM32

> **ETRS606 — IA Embarquée · L3 Réseaux & Télécoms**  
> Prise en main de la carte NUCLEO-N657X0, lecture de capteurs MEMS via I2C, connectivité réseau Ethernet.

---

## Matériel utilisé

- **NUCLEO-N657X0** — ARM Cortex-M33, 160 MHz, 320 Ko RAM, 512 Ko Flash, NPU Neural-ART
- **X-NUCLEO-IKS01A3** — Shield capteurs MEMS (I2C)

---

## 1. LED Blink

Première prise en main de la carte via STM32CubeIDE.

- Allumage des 3 LEDs (rouge, vert, bleu)
- Chenillard avec temporisation de 3 secondes
- Logs console via UART : `<début d'application>`, `<ROUGE>`, `<VERT>`, `<BLEU>`...

---

## 2. Interface Capteurs

Lecture des capteurs MEMS via bus I2C en utilisant les drivers STMicroelectronics.

**Drivers utilisés** (depuis [STMems_Standard_C_drivers](https://github.com/STMicroelectronics/STMems_Standard_C_drivers)) :

| Capteur | Grandeur | Driver |
|---------|----------|--------|
| HTS221 | Température + Humidité | `hts221_reg.c/h` |
| LPS22HH | Pression atmosphérique | `lps22hh_reg.c/h` |
| LSM6DSO | Accélération + Gyroscope | `lsm6dso_reg.c/h` |

**Implémentation :**
- Configuration I2C via CubeMX (PA12 → SCL, PA11 → SDA)
- Implémentation des fonctions bas-niveau `platform_read` / `platform_write` avec `HAL_I2C_Mem_Read` / `HAL_I2C_Mem_Write`
- Attention aux adresses 7-bit vs 8-bit (décalage `<< 1` pour HAL)
- Affichage des valeurs via UART/printf (activation `_printf_float` pour les floats)

**Indicateurs LED :**
- 🔴 Rouge — lecture capteurs en cours
- 🟢 Vert — disponible, en attente

---

## 3. Réseau Ethernet

Connectivité réseau via le connecteur RJ45 de la NUCLEO-N657X0.

**Stack utilisée :** FreeRTOS + LwIP (DHCP + TCP/IP)

**Approche :** Utilisation du template STM32CubeIDE  
`Access to Example Selector → NX TCP Echo Client`

Le template fournit une base FreeRTOS + LwIP préconfigurée avec socket BSD, ce qui a permis de valider la connectivité réseau sans repartir de zéro sur la configuration de la pile IP.

**Points techniques notables :**
- CMSISv2 : augmentation de la pile du thread `ethernetif_input` (1024 → 2048) pour éviter les stack overflow
- LwIP : `MEM_SIZE` augmenté à 5000 bytes
- Activation `LWIP_DEBUG` dans `lwipopts.h` pour diagnostiquer
- CRC hardware activé dans CubeMX pour corriger les erreurs sur les paquets ICMP

**Indicateurs LED :**
- 🔵 Bleu — communication réseau active

---

## Notes

- Le ping vers `8.8.8.8` fonctionne depuis la carte
- La connectivité validée ici sert de base pour le TP3 (envoi HTTP POST vers l'API AtmosAI)

## 👤 Auteur

William / Franck / Mostapha — L3 Réseaux & Télécoms  
Module ETRS606 — IA Embarquée  
Université Savoie Mont Blanc