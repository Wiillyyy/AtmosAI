/* =============================================================
 * TP4 — app_netxduo.c
 * Acquisition capteurs + inférence MLP H+1 embarquée
 *
 * Ajouts vs TP3 :
 *   - h1_inference.h : ring buffer + calcul 13 features + forward pass
 *   - h1_infer() appelé à chaque cycle capteur
 *   - JSON étendu avec prediction_h1 + confidence_h1
 *   - LEDs selon la classe prédite
 *   - Mesure DWT de la durée d'inférence
 *
 * Tentative NPU (abandonnée) :
 *   On a d'abord essayé d'utiliser le runtime LL_ATON généré par
 *   X-CUBE-AI. Au premier appel memcpy vers 0x342e0000 (AXISRAM5),
 *   BusFault immédiat : cette zone SRAM est câblée exclusivement
 *   sur le bus AXI du NPU, le CPU n'y a pas accès.
 *   Tous les blocs étant EpochBlock_Flags_pure_sw de toute façon,
 *   h1_infer() (C float32 pur) donne un résultat strictement
 *   identique sans aucune dépendance mémoire problématique.
 * ============================================================= */

#include "app_netxduo.h"
#include "main.h"
#include "hts221_reg.h"
#include "lps22hh_reg.h"
#include "h1_inference.h"   /* ← Ajout TP4 : MLP embarqué */
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Threads ────────────────────────────────────────────────── */
TX_THREAD      NxAppThread;
TX_THREAD      AppTCPThread;
TX_THREAD      AppSensorThread;

NX_PACKET_POOL NxAppPool;
NX_IP          NetXDuoEthIpInstance;
TX_SEMAPHORE   DHCPSemaphore;
NX_DHCP        DHCPClient;
NX_TCP_SOCKET  TCPSocket;

ULONG IpAddress;
ULONG NetMask;

/* Données partagées — mesures + prédiction H+1 */
volatile float g_temperature      = 0.0f;
volatile float g_humidity         = 0.0f;
volatile float g_pressure         = 0.0f;
volatile char  g_prediction_h1[16]= "";     /* ← Ajout TP4 */
volatile float g_confidence_h1    = 0.0f;  /* ← Ajout TP4 */

/* ── Prototypes ─────────────────────────────────────────────── */
static VOID nx_app_thread_entry(ULONG thread_input);
static VOID App_TCP_Thread_Entry(ULONG thread_input);
static VOID App_Sensor_Thread_Entry(ULONG thread_input);
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr);

/* ── Callbacks I2C ──────────────────────────────────────────── */
int32_t hts221_write(void *h, uint8_t reg, const uint8_t *buf, uint16_t len) {
    return (HAL_I2C_Mem_Write((I2C_HandleTypeDef*)h, HTS221_I2C_ADDRESS,
                               reg, I2C_MEMADD_SIZE_8BIT, (uint8_t*)buf, len, 1000) == HAL_OK) ? 0 : -1;
}
int32_t hts221_read(void *h, uint8_t reg, uint8_t *buf, uint16_t len) {
    reg |= 0x80;
    return (HAL_I2C_Mem_Read((I2C_HandleTypeDef*)h, HTS221_I2C_ADDRESS,
                               reg, I2C_MEMADD_SIZE_8BIT, buf, len, 1000) == HAL_OK) ? 0 : -1;
}
int32_t lps22hh_write(void *h, uint8_t reg, const uint8_t *buf, uint16_t len) {
    return (HAL_I2C_Mem_Write((I2C_HandleTypeDef*)h, LPS22HH_I2C_ADD_H,
                               reg, I2C_MEMADD_SIZE_8BIT, (uint8_t*)buf, len, 1000) == HAL_OK) ? 0 : -1;
}
int32_t lps22hh_read(void *h, uint8_t reg, uint8_t *buf, uint16_t len) {
    return (HAL_I2C_Mem_Read((I2C_HandleTypeDef*)h, LPS22HH_I2C_ADD_H,
                               reg, I2C_MEMADD_SIZE_8BIT, buf, len, 1000) == HAL_OK) ? 0 : -1;
}

/* ============================================================
 * Init NetXDuo
 * ============================================================ */
UINT MX_NetXDuo_Init(VOID *memory_ptr)
{
    UINT ret = NX_SUCCESS;
    TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;
    CHAR *pointer;

    printf("  TP4 — MLP H+1 embarque\r\n");
    printf("  13 features | ring buffer 560 ech. (~3h)\r\n");
    printf("  Inference : < 1 ms | Charge CPU : < 1 %%\r\n\r\n");

    nx_system_initialize();

    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_PACKET_POOL_SIZE, TX_NO_WAIT);
    nx_packet_pool_create(&NxAppPool, "App Pool", DEFAULT_PAYLOAD_SIZE,
                          pointer, NX_APP_PACKET_POOL_SIZE);

    tx_byte_allocate(byte_pool, (VOID**)&pointer, Nx_IP_INSTANCE_THREAD_SIZE, TX_NO_WAIT);
    nx_ip_create(&NetXDuoEthIpInstance, "NetX IP", NX_APP_DEFAULT_IP_ADDRESS,
                 NX_APP_DEFAULT_NET_MASK, &NxAppPool, nx_stm32_eth_driver,
                 pointer, Nx_IP_INSTANCE_THREAD_SIZE, NX_APP_INSTANCE_PRIORITY);

    tx_byte_allocate(byte_pool, (VOID**)&pointer, DEFAULT_ARP_CACHE_SIZE, TX_NO_WAIT);
    nx_arp_enable(&NetXDuoEthIpInstance, pointer, DEFAULT_ARP_CACHE_SIZE);
    nx_icmp_enable(&NetXDuoEthIpInstance);
    nx_tcp_enable(&NetXDuoEthIpInstance);
    nx_udp_enable(&NetXDuoEthIpInstance);

    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&NxAppThread, "NetXDuo App Thread", nx_app_thread_entry, 0,
                     pointer, NX_APP_THREAD_STACK_SIZE,
                     NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&AppTCPThread, "TCP Thread", App_TCP_Thread_Entry, 0,
                     pointer, NX_APP_THREAD_STACK_SIZE,
                     NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY,
                     TX_NO_TIME_SLICE, TX_DONT_START);

    /* Stack statique pour le thread capteur — évite tout débordement byte pool */
    static UCHAR s_sensor_stack[4096];
    tx_thread_create(&AppSensorThread, "Sensor Thread", App_Sensor_Thread_Entry, 0,
                     s_sensor_stack, sizeof(s_sensor_stack),
                     NX_APP_THREAD_PRIORITY + 1, NX_APP_THREAD_PRIORITY + 1,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    nx_dhcp_create(&DHCPClient, &NetXDuoEthIpInstance, "DHCP Client");
    tx_semaphore_create(&DHCPSemaphore, "DHCP Semaphore", 0);

    return ret;
}

/* ============================================================
 * Thread principal — DHCP puis démarrage TCP
 * ============================================================ */
static VOID nx_app_thread_entry(ULONG thread_input)
{
    nx_ip_address_change_notify(&NetXDuoEthIpInstance,
                                ip_address_change_notify_callback, NULL);
    nx_dhcp_start(&DHCPClient);
    printf("Attente adresse DHCP...\r\n");
    tx_semaphore_get(&DHCPSemaphore, TX_WAIT_FOREVER);
    PRINT_IP_ADDRESS(IpAddress);
    tx_thread_resume(&AppTCPThread);
    tx_thread_relinquish();
}

static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr)
{
    if (nx_ip_address_get(&NetXDuoEthIpInstance, &IpAddress, &NetMask) == NX_SUCCESS)
        if (IpAddress != 0) tx_semaphore_put(&DHCPSemaphore);
}

/* ============================================================
 * Thread capteurs — lecture + ring buffer + inférence H+1
 * ============================================================ */
static VOID App_Sensor_Thread_Entry(ULONG thread_input)
{
    extern I2C_HandleTypeDef hi2c1;
    stmdev_ctx_t ctx_hts, ctx_lps;

    ctx_hts.write_reg = hts221_write; ctx_hts.read_reg = hts221_read;
    ctx_hts.handle    = &hi2c1;
    ctx_lps.write_reg = lps22hh_write; ctx_lps.read_reg = lps22hh_read;
    ctx_lps.handle    = &hi2c1;

    lps22hh_block_data_update_set(&ctx_lps, PROPERTY_ENABLE);
    lps22hh_data_rate_set(&ctx_lps, LPS22HH_1_Hz_LOW_NOISE);
    hts221_block_data_update_set(&ctx_hts, PROPERTY_ENABLE);
    hts221_power_on_set(&ctx_hts, PROPERTY_ENABLE);
    hts221_data_rate_set(&ctx_hts, HTS221_ODR_1Hz);

    /* Calibration HTS221 */
    float T0_degC, T1_degC, H0_rh, H1_rh;
    int16_t T0_out, T1_out, H0_T0_out, H1_T0_out;
    uint8_t b0, b1, msb;
    hts221_read_reg(&ctx_hts, 0x30, &b0, 1); H0_rh = b0 / 2.0f;
    hts221_read_reg(&ctx_hts, 0x31, &b0, 1); H1_rh = b0 / 2.0f;
    hts221_read_reg(&ctx_hts, 0x32, &b0, 1);
    hts221_read_reg(&ctx_hts, 0x33, &b1, 1);
    hts221_read_reg(&ctx_hts, 0x35, &msb, 1);
    T0_degC = (float)(b0 | ((msb & 0x03) << 8)) / 8.0f;
    T1_degC = (float)(b1 | ((msb & 0x0C) << 6)) / 8.0f;
    hts221_read_reg(&ctx_hts, 0x36, &b0, 1); hts221_read_reg(&ctx_hts, 0x37, &b1, 1);
    H0_T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&ctx_hts, 0x3A, &b0, 1); hts221_read_reg(&ctx_hts, 0x3B, &b1, 1);
    H1_T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&ctx_hts, 0x3C, &b0, 1); hts221_read_reg(&ctx_hts, 0x3D, &b1, 1);
    T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&ctx_hts, 0x3E, &b0, 1); hts221_read_reg(&ctx_hts, 0x3F, &b1, 1);
    T1_out = (int16_t)(((uint16_t)b1 << 8) | b0);

    /* ── Ajout TP4 : init ring buffer + DWT ─────────────────── */
    h1_init();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    printf("[H1] Ring buffer initialise (560 echantillons / ~3h)\r\n");
    printf("[H1] Pret pour l'inference MLP\r\n");

    while (1)
    {
        /* Lecture capteurs */
        uint8_t lps_st = 0, hts_st = 0;
        uint32_t raw_p = 0; int16_t raw_t = 0, raw_h = 0;

        lps22hh_read_reg(&ctx_lps, LPS22HH_STATUS, &lps_st, 1);
        if (lps_st & 0x01) {
            lps22hh_pressure_raw_get(&ctx_lps, &raw_p);
            g_pressure = lps22hh_from_lsb_to_hpa(raw_p);
        }
        hts221_read_reg(&ctx_hts, HTS221_STATUS_REG, &hts_st, 1);
        if (hts_st & 0x02) {
            hts221_temperature_raw_get(&ctx_hts, &raw_t);
            g_temperature = (raw_t - T0_out) * (T1_degC - T0_degC)
                          / (T1_out - T0_out) + T0_degC;
        }
        if (hts_st & 0x01) {
            hts221_humidity_raw_get(&ctx_hts, &raw_h);
            g_humidity = (raw_h - H0_T0_out) * (H1_rh - H0_rh)
                       / (H1_T0_out - H0_T0_out) + H0_rh;
        }

        printf("[SENSOR] T=%.2f C  H=%.2f %%  P=%.2f hPa\r\n",
               g_temperature, g_humidity, g_pressure);

        /* ── Ajout TP4 : push ring buffer + inférence ─────────── */
        h1_push(g_temperature, g_humidity, g_pressure);

        uint32_t t0 = DWT->CYCCNT;
        H1Result res = h1_infer();
        uint32_t infer_cycles = DWT->CYCCNT - t0;
        float    infer_us     = (float)infer_cycles / ((float)SystemCoreClock / 1e6f);

        if (res.ready) {
            const char *cls = h1_class_name(res.label);
            strncpy((char*)g_prediction_h1, cls, sizeof(g_prediction_h1) - 1);
            g_confidence_h1 = res.confidence;

            printf("[H1] Prediction : %s  (conf=%.1f%%)  "
                   "scores C=%.2f P=%.2f N=%.2f  "
                   "inference=%.2f us\r\n",
                   cls, res.confidence * 100.0f,
                   res.scores[0], res.scores[1], res.scores[2],
                   infer_us);

            /* LEDs selon la prédiction (actif bas) */
            HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_SET);
            if (res.label == H1_CLASS_CLAIR)
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
            else if (res.label == H1_CLASS_PLUIE)
                HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
            else {
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_RESET);
            }
        } else {
            printf("[H1] Historique insuffisant (ring buffer en cours de remplissage)\r\n");
        }

        /* Clignotement ~20s */
        for (int b = 0; b < 4; b++) {
            tx_thread_sleep(480);
            HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
            tx_thread_sleep(20);
            HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
        }
    }
}

/* ============================================================
 * Thread TCP — envoie mesures + prédiction H+1 toutes les 20s
 * TP4 : JSON étendu avec prediction_h1 + confidence_h1
 * ============================================================ */
static VOID App_TCP_Thread_Entry(ULONG thread_input)
{
    UINT  ret;
    ULONG bytes_read;
    static UCHAR data_buffer[512];
    NX_PACKET *pkt_rx, *pkt_tx;
    static char json[256], http[512];

    ULONG server_ip   = IP_ADDRESS(45, 155, 170, 159);
    UINT  server_port = 5080;

    ret = nx_tcp_socket_create(&NetXDuoEthIpInstance, &TCPSocket,
                               "TCP Client", NX_IP_NORMAL, NX_FRAGMENT_OKAY,
                               NX_IP_TIME_TO_LIVE, 1024, NX_NULL, NX_NULL);
    if (ret != NX_SUCCESS) Error_Handler();
    nx_tcp_client_socket_bind(&TCPSocket, NX_ANY_PORT, NX_WAIT_FOREVER);
    printf("Thread TCP demarre.\r\n");

    while (1)
    {
        tx_thread_sleep(500);

        ret = nx_tcp_client_socket_connect(&TCPSocket, server_ip, server_port,
                                           10 * NX_IP_PERIODIC_RATE);
        if (ret == NX_SUCCESS)
        {
            /* JSON TP4 — inclut maintenant la prédiction embarquée */
            int jlen = snprintf(json, sizeof(json),
                "{\"device_id\":\"NUCLEO-N657X0\","
                "\"temperature\":%.1f,"
                "\"humidity\":%.1f,"
                "\"pressure\":%.1f,"
                "\"prediction_h1\":\"%s\","       /* ← Ajout TP4 */
                "\"confidence_h1\":%.4f}",        /* ← Ajout TP4 */
                g_temperature, g_humidity, g_pressure,
                (g_prediction_h1[0] ? (const char*)g_prediction_h1 : ""),
                (double)g_confidence_h1);

            int rlen = snprintf(http, sizeof(http),
                "POST /api/data HTTP/1.1\r\n"
                "Host: atmosai.willydev.xyz\r\n"
                "Content-Type: application/json\r\n"
                "X-API-Key: atmosai_w1lly_2026\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n\r\n"
                "%s", jlen, json);

            ret = nx_packet_allocate(&NxAppPool, &pkt_tx, NX_TCP_PACKET, TX_WAIT_FOREVER);
            if (ret == NX_SUCCESS) {
                nx_packet_data_append(pkt_tx, http, rlen, &NxAppPool, TX_WAIT_FOREVER);
                nx_tcp_socket_send(&TCPSocket, pkt_tx, 5 * NX_IP_PERIODIC_RATE);
                ret = nx_tcp_socket_receive(&TCPSocket, &pkt_rx, 5 * NX_IP_PERIODIC_RATE);
                if (ret == NX_SUCCESS) {
                    nx_packet_data_retrieve(pkt_rx, data_buffer, &bytes_read);
                    data_buffer[bytes_read] = '\0';
                    if (strstr((char*)data_buffer, "201"))
                        printf("[POST] OK 201 -> VPS  (pred=%s conf=%.1f%%)\r\n",
                               (char*)g_prediction_h1, g_confidence_h1 * 100.0f);
                    nx_packet_release(pkt_rx);
                }
            }
            nx_tcp_socket_disconnect(&TCPSocket, 5 * NX_IP_PERIODIC_RATE);
        }
        else {
            printf("[TCP] Erreur connexion : 0x%02X\r\n", ret);
        }

        tx_thread_sleep(2000);
    }
}
