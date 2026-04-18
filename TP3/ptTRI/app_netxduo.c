/* =============================================================
 * TP3 — app_netxduo.c
 * Acquisition capteurs HTS221 + LPS22HH + envoi HTTP POST Flask
 * Pas d'inférence IA à ce stade.
 * ============================================================= */

#include "app_netxduo.h"
#include "main.h"
#include "hts221_reg.h"
#include "lps22hh_reg.h"
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

/* Données capteurs partagées entre les deux threads */
volatile float g_temperature = 0.0f;
volatile float g_humidity    = 0.0f;
volatile float g_pressure    = 0.0f;

/* ── Prototypes ─────────────────────────────────────────────── */
static VOID nx_app_thread_entry(ULONG thread_input);
static VOID App_TCP_Thread_Entry(ULONG thread_input);
static VOID App_Sensor_Thread_Entry(ULONG thread_input);
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr);

/* ── Callbacks I2C (requis par les drivers ST MEMS) ─────────── */
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

    printf("  TP3 — Station meteo embarquee\r\n");
    printf("  Capteurs : HTS221 (T/H) + LPS22HH (P)\r\n");
    printf("  Protocole : HTTP POST → API Flask VPS\r\n\r\n");

    nx_system_initialize();

    /* Packet pool */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_PACKET_POOL_SIZE, TX_NO_WAIT);
    nx_packet_pool_create(&NxAppPool, "App Pool", DEFAULT_PAYLOAD_SIZE,
                          pointer, NX_APP_PACKET_POOL_SIZE);

    /* IP instance */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, Nx_IP_INSTANCE_THREAD_SIZE, TX_NO_WAIT);
    nx_ip_create(&NetXDuoEthIpInstance, "NetX IP", NX_APP_DEFAULT_IP_ADDRESS,
                 NX_APP_DEFAULT_NET_MASK, &NxAppPool, nx_stm32_eth_driver,
                 pointer, Nx_IP_INSTANCE_THREAD_SIZE, NX_APP_INSTANCE_PRIORITY);

    /* ARP + ICMP + TCP + UDP */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, DEFAULT_ARP_CACHE_SIZE, TX_NO_WAIT);
    nx_arp_enable(&NetXDuoEthIpInstance, pointer, DEFAULT_ARP_CACHE_SIZE);
    nx_icmp_enable(&NetXDuoEthIpInstance);
    nx_tcp_enable(&NetXDuoEthIpInstance);
    nx_udp_enable(&NetXDuoEthIpInstance);

    /* Thread principal (DHCP) */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&NxAppThread, "NetXDuo App Thread", nx_app_thread_entry, 0,
                     pointer, NX_APP_THREAD_STACK_SIZE,
                     NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    /* Thread TCP */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&AppTCPThread, "TCP Thread", App_TCP_Thread_Entry, 0,
                     pointer, NX_APP_THREAD_STACK_SIZE,
                     NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY,
                     TX_NO_TIME_SLICE, TX_DONT_START);

    /* Thread capteurs */
    tx_byte_allocate(byte_pool, (VOID**)&pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&AppSensorThread, "Sensor Thread", App_Sensor_Thread_Entry, 0,
                     pointer, NX_APP_THREAD_STACK_SIZE,
                     NX_APP_THREAD_PRIORITY + 1, NX_APP_THREAD_PRIORITY + 1,
                     TX_NO_TIME_SLICE, TX_AUTO_START);

    /* DHCP */
    nx_dhcp_create(&DHCPClient, &NetXDuoEthIpInstance, "DHCP Client");
    tx_semaphore_create(&DHCPSemaphore, "DHCP Semaphore", 0);

    return ret;
}

/* ============================================================
 * Thread principal — attend l'IP DHCP puis démarre le TCP
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
 * Thread capteurs — lecture HTS221 + LPS22HH toutes les 20s
 * ============================================================ */
static VOID App_Sensor_Thread_Entry(ULONG thread_input)
{
    extern I2C_HandleTypeDef hi2c1;
    stmdev_ctx_t ctx_hts, ctx_lps;

    ctx_hts.write_reg = hts221_write;  ctx_hts.read_reg = hts221_read;
    ctx_hts.handle    = &hi2c1;
    ctx_lps.write_reg = lps22hh_write; ctx_lps.read_reg = lps22hh_read;
    ctx_lps.handle    = &hi2c1;

    /* Configuration capteurs */
    lps22hh_block_data_update_set(&ctx_lps, PROPERTY_ENABLE);
    lps22hh_data_rate_set(&ctx_lps, LPS22HH_1_Hz_LOW_NOISE);
    hts221_block_data_update_set(&ctx_hts, PROPERTY_ENABLE);
    hts221_power_on_set(&ctx_hts, PROPERTY_ENABLE);
    hts221_data_rate_set(&ctx_hts, HTS221_ODR_1Hz);

    /* Calibration HTS221 (coefficients constructeur en registres) */
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

    printf("Capteurs initialises.\r\n");

    while (1)
    {
        uint8_t lps_st = 0, hts_st = 0;
        uint32_t raw_p = 0;
        int16_t  raw_t = 0, raw_h = 0;

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

        /* Allume LED verte pour signaler une mesure */
        HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
        tx_thread_sleep(10);
        HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);

        tx_thread_sleep(2000 - 10); /* cycle ~20s */
    }
}

/* ============================================================
 * Thread TCP — envoie les mesures en HTTP POST toutes les 20s
 * TP3 : pas de champ prediction_h1, JSON simple
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
            /* JSON TP3 — mesures brutes uniquement, pas de prédiction */
            int jlen = snprintf(json, sizeof(json),
                "{\"device_id\":\"NUCLEO-N657X0\","
                "\"temperature\":%.1f,"
                "\"humidity\":%.1f,"
                "\"pressure\":%.1f}",
                g_temperature, g_humidity, g_pressure);

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
                        printf("[POST] OK 201 -> VPS\r\n");
                    else
                        printf("[POST] Reponse : %.40s\r\n", data_buffer);
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
