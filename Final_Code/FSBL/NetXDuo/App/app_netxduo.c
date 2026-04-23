/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_netxduo.c
  * @author  MCD Application Team
  * @brief   NetXDuo applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_netxduo.h"
#include <stdio.h>
#include <math.h>

/* Private includes ----------------------------------------------------------*/
#include "nxd_dhcp_client.h"
/* USER CODE BEGIN Includes */
#include "main.h"
#include "hts221_reg.h"
#include "lps22hh_reg.h"
#include "h1_inference.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TX_THREAD      NxAppThread;
NX_PACKET_POOL NxAppPool;
NX_IP          NetXDuoEthIpInstance;
TX_SEMAPHORE   DHCPSemaphore;
NX_DHCP        DHCPClient;
/* USER CODE BEGIN PV */
TX_THREAD AppTCPThread;
TX_THREAD AppLinkThread;
TX_THREAD AppSensorThread;

NX_TCP_SOCKET TCPSocket;

ULONG          IpAddress;
ULONG          NetMask;
volatile float g_temperature    = 0.0f;
volatile float g_humidity       = 0.0f;
volatile float g_pressure       = 0.0f;
volatile char  g_prediction_h1[16] = "";
volatile float g_confidence_h1  = 0.0f;
volatile float g_cpu_load_pct   = 0.0f;
volatile float g_infer_time_us  = 0.0f;
volatile float g_power_mw       = 0.0f;
volatile float g_cycle_ms       = 0.0f;
volatile unsigned long g_uptime_s = 0;
volatile unsigned long g_post_ok_count = 0;
volatile unsigned long g_post_fail_count = 0;
volatile unsigned long g_sample_seq = 0;
volatile char  g_post_status[16] = "idle";
volatile char  g_server_cmd[16] = "none";   /* Downlink VPS → carte */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static VOID nx_app_thread_entry (ULONG thread_input);
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr);
/* USER CODE BEGIN PFP */
static VOID App_TCP_Thread_Entry(ULONG thread_input);
static VOID App_Link_Thread_Entry(ULONG thread_input);
static VOID App_Sensor_Thread_Entry(ULONG thread_input);
static void App_Try_Init_Npu(void);
extern void LL_ATON_RT_RuntimeInit(void);
/* USER CODE END PFP */
/* --- COLLE LES 4 FONCTIONS I2C ICI --- */
int32_t hts221_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
    if (HAL_I2C_Mem_Write((I2C_HandleTypeDef*)handle, HTS221_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t*)bufp, len, 1000) == HAL_OK)
        return 0;
    return -1;
}

int32_t hts221_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
    reg |= 0x80; // auto-increment
    if (HAL_I2C_Mem_Read((I2C_HandleTypeDef*)handle, HTS221_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, bufp, len, 1000) == HAL_OK)
        return 0;
    return -1;
}

int32_t lps22hh_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
    if (HAL_I2C_Mem_Write((I2C_HandleTypeDef*)handle, LPS22HH_I2C_ADD_H, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t*)bufp, len, 1000) == HAL_OK)
        return 0;
    return -1;
}

int32_t lps22hh_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
    if (HAL_I2C_Mem_Read((I2C_HandleTypeDef*)handle, LPS22HH_I2C_ADD_H, reg, I2C_MEMADD_SIZE_8BIT, bufp, len, 1000) == HAL_OK)
        return 0;
    return -1;
}


/* ------------------------------------- */

/**
  * @brief  Application NetXDuo Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_NetXDuo_Init(VOID *memory_ptr)
{
  UINT ret = NX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;
  CHAR *pointer;

  /* USER CODE BEGIN MX_NetXDuo_MEM_POOL */

  /* USER CODE END MX_NetXDuo_MEM_POOL */
  /* USER CODE BEGIN 0 */
  printf("\r\n");
  printf("  ___  _                      _   ___ \r\n");
  printf(" / _ \\| |_ _ __  _____   __ _| | |_ _|\r\n");
  printf("| |_| | __| '_ \\/ _ \\ \\ / _` | |  | | \r\n");
  printf("|  _  | |_| | | | (_) | | (_| | |  | | \r\n");
  printf("|_| |_|\\__|_| |_|\\___/ \\_\\__,_|_| |___|\r\n");
  printf("\r\n");
  printf("  Station meteo embarquee — ETRS 606\r\n");
  printf("  STM32N657X0  |  Azure RTOS  |  MLP H+1\r\n");
  printf("\r\n");
  printf("  Equipe :\r\n");
  printf("    William Z.\r\n");
  printf("    Franck G.\r\n");
  printf("    Mostapha K.\r\n");
  printf("\r\n");
  printf("  ==========================================\r\n");
  printf("\r\n");
  /* USER CODE END 0 */

  /* Initialize the NetXDuo system. */
  nx_system_initialize();

    /* Allocate the memory for packet_pool.  */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, NX_APP_PACKET_POOL_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create the Packet pool to be used for packet allocation,
   * If extra NX_PACKET are to be used the NX_APP_PACKET_POOL_SIZE should be increased
   */
  ret = nx_packet_pool_create(&NxAppPool, "NetXDuo App Pool", DEFAULT_PAYLOAD_SIZE, pointer, NX_APP_PACKET_POOL_SIZE);

  if (ret != NX_SUCCESS)
  {
    return NX_POOL_ERROR;
  }

    /* Allocate the memory for Ip_Instance */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, Nx_IP_INSTANCE_THREAD_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

   /* Create the main NX_IP instance */
  ret = nx_ip_create(&NetXDuoEthIpInstance, "NetX Ip instance", NX_APP_DEFAULT_IP_ADDRESS, NX_APP_DEFAULT_NET_MASK, &NxAppPool, nx_stm32_eth_driver,
                     pointer, Nx_IP_INSTANCE_THREAD_SIZE, NX_APP_INSTANCE_PRIORITY);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

    /* Allocate the memory for ARP */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, DEFAULT_ARP_CACHE_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Enable the ARP protocol and provide the ARP cache size for the IP instance */

  /* USER CODE BEGIN ARP_Protocol_Initialization */

  /* USER CODE END ARP_Protocol_Initialization */

  ret = nx_arp_enable(&NetXDuoEthIpInstance, (VOID *)pointer, DEFAULT_ARP_CACHE_SIZE);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

  /* Enable the ICMP */

  /* USER CODE BEGIN ICMP_Protocol_Initialization */

  /* USER CODE END ICMP_Protocol_Initialization */

  ret = nx_icmp_enable(&NetXDuoEthIpInstance);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

  /* Enable TCP Protocol */

  /* USER CODE BEGIN TCP_Protocol_Initialization */
  /* Allocate the memory for TCP server thread   */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* create the TCP server thread */
  ret = tx_thread_create(&AppTCPThread, "App TCP Thread", App_TCP_Thread_Entry, 0, pointer, NX_APP_THREAD_STACK_SIZE,
                         NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY, TX_NO_TIME_SLICE, TX_DONT_START);

  if (ret != TX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }
  /* USER CODE END TCP_Protocol_Initialization */

  ret = nx_tcp_enable(&NetXDuoEthIpInstance);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

  /* Enable the UDP protocol required for  DHCP communication */

  /* USER CODE BEGIN UDP_Protocol_Initialization */

  /* USER CODE END UDP_Protocol_Initialization */

  ret = nx_udp_enable(&NetXDuoEthIpInstance);

  if (ret != NX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

   /* Allocate the memory for main thread   */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* Create the main thread */
  ret = tx_thread_create(&NxAppThread, "NetXDuo App thread", nx_app_thread_entry , 0, pointer, NX_APP_THREAD_STACK_SIZE,
                         NX_APP_THREAD_PRIORITY, NX_APP_THREAD_PRIORITY, TX_NO_TIME_SLICE, TX_AUTO_START);

  if (ret != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  /* Create the DHCP client */

  /* USER CODE BEGIN DHCP_Protocol_Initialization */

  /* USER CODE END DHCP_Protocol_Initialization */

  ret = nx_dhcp_create(&DHCPClient, &NetXDuoEthIpInstance, "DHCP Client");

  if (ret != NX_SUCCESS)
  {
    return NX_DHCP_ERROR;
  }

  /* set DHCP notification callback  */
  ret = tx_semaphore_create(&DHCPSemaphore, "DHCP Semaphore", 0);

    if (ret != NX_SUCCESS)
    {
      return NX_DHCP_ERROR;
    }

  /* USER CODE BEGIN MX_NetXDuo_Init */
  /* Allocate the memory for Link thread   */
    if (tx_byte_allocate(byte_pool, (VOID **) &pointer, NX_APP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return TX_POOL_ERROR;
  }

  /* create the Link thread */
    ret = tx_thread_create(&AppSensorThread, "App Sensor Thread", App_Sensor_Thread_Entry, 0,
    		pointer, NX_APP_THREAD_STACK_SIZE,
			NX_APP_THREAD_PRIORITY + 1, NX_APP_THREAD_PRIORITY + 1,
			TX_NO_TIME_SLICE, TX_AUTO_START);

  if (ret != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }
  /* USER CODE END MX_NetXDuo_Init */

  return ret;
}

/**
* @brief  ip address change callback.
* @param ip_instance: NX_IP instance
* @param ptr: user data
* @retval none
*/
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr)
{
  /* USER CODE BEGIN ip_address_change_notify_callback */
  /* release the semaphore as soon as an IP address is available */
  if (nx_ip_address_get(&NetXDuoEthIpInstance, &IpAddress, &NetMask) != NX_SUCCESS)
  {
    /* USER CODE BEGIN IP address change callback error */
    Error_Handler();
    /* USER CODE END IP address change callback error */
  }
  if(IpAddress != NULL_ADDRESS)
  {
  tx_semaphore_put(&DHCPSemaphore);
  }
  /* USER CODE END ip_address_change_notify_callback */
}

/**
* @brief  Main thread entry.
* @param thread_input: ULONG user argument used by the thread entry
* @retval none
*/
static VOID nx_app_thread_entry (ULONG thread_input)
{
  /* USER CODE BEGIN Nx_App_Thread_Entry 0 */

  /* USER CODE END Nx_App_Thread_Entry 0 */

  UINT ret = NX_SUCCESS;

  /* USER CODE BEGIN Nx_App_Thread_Entry 1 */

  /* USER CODE END Nx_App_Thread_Entry 1 */

  /* register the IP address change callback */
  ret = nx_ip_address_change_notify(&NetXDuoEthIpInstance, ip_address_change_notify_callback, NULL);
  if (ret != NX_SUCCESS)
  {
    /* USER CODE BEGIN IP address change callback error */
    Error_Handler();
    /* USER CODE END IP address change callback error */
  }

  /* start the DHCP client */
  ret = nx_dhcp_start(&DHCPClient);
  if (ret != NX_SUCCESS)
  {
    /* USER CODE BEGIN DHCP client start error */
    Error_Handler();
    /* USER CODE END DHCP client start error */
  }
   printf("[NET] Recherche serveur DHCP...\r\n");
  /* wait until an IP address is ready */
  if(tx_semaphore_get(&DHCPSemaphore, TX_WAIT_FOREVER) != TX_SUCCESS)
  {
    /* USER CODE BEGIN DHCPSemaphore get error */
    Error_Handler();
    /* USER CODE END DHCPSemaphore get error */
  }

  /* --- AJOUTS POUR LA GATEWAY --- */
    UCHAR gateway_buf[4];
    UINT gateway_size = 4;

    if (nx_dhcp_user_option_retrieve(&DHCPClient, 3, gateway_buf, &gateway_size) == NX_SUCCESS)
    {
        /* On remet les octets dans le bon sens */
        ULONG gateway_ip = IP_ADDRESS(gateway_buf[3], gateway_buf[2], gateway_buf[1], gateway_buf[0]);
        nx_ip_gateway_address_set(&NetXDuoEthIpInstance, gateway_ip);

        printf("[NET] Passerelle DHCP : %d.%d.%d.%d\r\n",
               gateway_buf[3], gateway_buf[2], gateway_buf[1], gateway_buf[0]);
    }
    else
    {
        printf("[NET] Passerelle DHCP indisponible\r\n");
    }
    /* ------------------------------ */

  /* USER CODE BEGIN Nx_App_Thread_Entry 2 */

  PRINT_IP_ADDRESS(IpAddress);

  /* the network is correctly initialized, start the TCP server thread */
  tx_thread_resume(&AppTCPThread);

  /* if this thread is not needed any more, we relinquish it */
  tx_thread_relinquish();

  return;
  /* USER CODE END Nx_App_Thread_Entry 2 */

}
/* USER CODE BEGIN 1 */
/**
* @brief  TCP thread entry.
* @param thread_input: thread user data
* @retval none
*/

static VOID App_Sensor_Thread_Entry(ULONG thread_input)
{
    extern I2C_HandleTypeDef hi2c1;
    static unsigned long s_cycle_id = 0;

    // Fonctions I2C
    stmdev_ctx_t dev_ctx_hts221, dev_ctx_lps22hh;

    // HTS221
    dev_ctx_hts221.write_reg = hts221_write;
    dev_ctx_hts221.read_reg  = hts221_read;
    dev_ctx_hts221.handle    = (void*)&hi2c1;

    // LPS22HH
    dev_ctx_lps22hh.write_reg = lps22hh_write;
    dev_ctx_lps22hh.read_reg  = lps22hh_read;
    dev_ctx_lps22hh.handle    = (void*)&hi2c1;

    // Config capteurs
    lps22hh_block_data_update_set(&dev_ctx_lps22hh, PROPERTY_ENABLE);
    lps22hh_data_rate_set(&dev_ctx_lps22hh, LPS22HH_1_Hz_LOW_NOISE);
    hts221_block_data_update_set(&dev_ctx_hts221, PROPERTY_ENABLE);
    hts221_power_on_set(&dev_ctx_hts221, PROPERTY_ENABLE);
    hts221_data_rate_set(&dev_ctx_hts221, HTS221_ODR_1Hz);

    // Calibration HTS221
    float T0_degC, T1_degC, H0_rh, H1_rh;
    int16_t T0_out, T1_out, H0_T0_out, H1_T0_out;
    uint8_t b0, b1, t0_t1_msb;

    hts221_read_reg(&dev_ctx_hts221, 0x30, &b0, 1); H0_rh = b0 / 2.0f;
    hts221_read_reg(&dev_ctx_hts221, 0x31, &b0, 1); H1_rh = b0 / 2.0f;
    hts221_read_reg(&dev_ctx_hts221, 0x32, &b0, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x33, &b1, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x35, &t0_t1_msb, 1);
    T0_degC = (float)(b0 | ((t0_t1_msb & 0x03) << 8)) / 8.0f;
    T1_degC = (float)(b1 | ((t0_t1_msb & 0x0C) << 6)) / 8.0f;
    hts221_read_reg(&dev_ctx_hts221, 0x36, &b0, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x37, &b1, 1);
    H0_T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&dev_ctx_hts221, 0x3A, &b0, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x3B, &b1, 1);
    H1_T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&dev_ctx_hts221, 0x3C, &b0, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x3D, &b1, 1);
    T0_out = (int16_t)(((uint16_t)b1 << 8) | b0);
    hts221_read_reg(&dev_ctx_hts221, 0x3E, &b0, 1);
    hts221_read_reg(&dev_ctx_hts221, 0x3F, &b1, 1);
    T1_out = (int16_t)(((uint16_t)b1 << 8) | b0);

    printf("[SNS] Capteurs initialises\r\n");

    App_Try_Init_Npu();

/* Initialise le ring buffer d'inférence H+1 */
    h1_init();

    /* ── DWT cycle counter (Cortex-M55) ── */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    printf("[PWR] DWT cycle counter actif  (Fclk=%lu MHz)\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));

    /* Variables de mesure inter-cycles */
    static ULONG    s_t_cycle_prev    = 0;
    static uint32_t s_active_cycles   = 0;
    static uint32_t s_infer_cycles    = 0;
    static uint8_t  s_first_cycle     = 1;

    while(1)
    {
        if (strcmp((char*)g_server_cmd, "dance") == 0)
        {
            const uint32_t dance_duration_ms = 20000UL;
            const uint32_t dance_toggle_ms = 80UL;
            const int dance_bar_width = 20;
            uint32_t dance_start_ms;
            uint32_t next_toggle_ms;
            int dance_phase = 0;
            int last_bar_bucket = -1;
            strncpy((char*)g_server_cmd, "none", sizeof(g_server_cmd));
            printf("\r\n[CMD] *** MODE DANSE ACTIF *** Pause cycle normal pendant 20 s\r\n");

            dance_start_ms = HAL_GetTick();
            next_toggle_ms = dance_start_ms;

            while ((HAL_GetTick() - dance_start_ms) < dance_duration_ms) {
                uint32_t now_ms = HAL_GetTick();
                uint32_t elapsed_ms = now_ms - dance_start_ms;

                if ((int32_t)(now_ms - next_toggle_ms) >= 0) {
                    dance_phase ^= 1;
                    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, dance_phase ? GPIO_PIN_RESET : GPIO_PIN_SET);
                    HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   dance_phase ? GPIO_PIN_SET   : GPIO_PIN_RESET);
                    next_toggle_ms += dance_toggle_ms;
                }

                {
                    int bucket = (int)((elapsed_ms * dance_bar_width) / dance_duration_ms);
                    if (bucket > dance_bar_width) bucket = dance_bar_width;
                    if (bucket != last_bar_bucket) {
                        char bar[32];
                        int remaining_s = (int)((dance_duration_ms - elapsed_ms + 999UL) / 1000UL);
                        if (remaining_s < 0) remaining_s = 0;
                        for (int i = 0; i < dance_bar_width; i++) {
                            bar[i] = (i < bucket) ? '#' : '.';
                        }
                        bar[dance_bar_width] = '\0';
                        printf("[CMD] Danse [%s] %2lu%%  ~%2ds restantes\r\n",
                               bar,
                               (unsigned long)((elapsed_ms * 100UL) / dance_duration_ms),
                               remaining_s);
                        last_bar_bucket = bucket;
                    }
                }

                tx_thread_sleep(1);
            }
            HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_SET);
            printf("[CMD] Mode danse termine -> reprise capteurs / inference\r\n");
            continue;
        }

        s_cycle_id++;

        /* ── Mesures du cycle précédent ── */
        ULONG t_now = tx_time_get();
        if (!s_first_cycle)
        {
            ULONG  period_ticks = t_now - s_t_cycle_prev;
            float  period_ms    = (float)period_ticks * (1000.0f / (float)TX_TIMER_TICKS_PER_SECOND);
            float  active_ms    = (float)s_active_cycles / ((float)SystemCoreClock / 1000.0f);
            float  cpu_load     = (period_ms > 0.0f) ? (active_ms / period_ms * 100.0f) : 0.0f;
            if (cpu_load > 100.0f) cpu_load = 100.0f;
            float  infer_us     = (float)s_infer_cycles / ((float)SystemCoreClock / 1000000.0f);
            /* Modele lineaire datasheet STM32N6 : 30 mA idle → 150 mA full load @ 3.3V */
            float  I_mA         = 30.0f + (cpu_load / 100.0f) * (150.0f - 30.0f);
            float  P_mW         = 3.3f * I_mA;
            g_cycle_ms = period_ms;
            g_cpu_load_pct = cpu_load;
            g_infer_time_us = infer_us;
            g_power_mw = P_mW;
            printf("[PWR] ----------------------------------\r\n");
            printf("[PWR] Periode cycle : %.0f ms\r\n",       period_ms);
            printf("[PWR] CPU load      : %.1f %%\r\n",       cpu_load);
            printf("[PWR] h1_infer()    : %.2f us  (%lu cyc)\r\n",
                   infer_us, (unsigned long)s_infer_cycles);
            printf("[PWR] I estimee     : %.1f mA\r\n",       I_mA);
            printf("[PWR] P estimee     : %.0f mW  (%.3f W)\r\n", P_mW, P_mW / 1000.0f);
            printf("[PWR] ----------------------------------\r\n\r\n");
        }
        s_first_cycle   = 0;
        s_t_cycle_prev  = t_now;
        g_uptime_s = (unsigned long)(HAL_GetTick() / 1000UL);
        uint32_t t_active_start = DWT->CYCCNT;   /* début partie active */

        printf("\r\n[CYCLE %lu] ==================================\r\n", s_cycle_id);

        uint8_t lps_status = 0, hts_status = 0;
        uint32_t raw_pressure = 0;
        int16_t raw_temperature = 0, raw_humidity = 0;

        // Pression
        lps22hh_read_reg(&dev_ctx_lps22hh, LPS22HH_STATUS, &lps_status, 1);
        if (lps_status & 0x01) {
            lps22hh_pressure_raw_get(&dev_ctx_lps22hh, &raw_pressure);
            g_pressure = lps22hh_from_lsb_to_hpa(raw_pressure);
            printf("[SNS] Pression    : %.2f hPa\r\n", g_pressure);
        }

        // Température + Humidité
        hts221_read_reg(&dev_ctx_hts221, HTS221_STATUS_REG, &hts_status, 1);
        if (hts_status & 0x02) {
            hts221_temperature_raw_get(&dev_ctx_hts221, &raw_temperature);
            g_temperature = (raw_temperature - T0_out) * (T1_degC - T0_degC) / (T1_out - T0_out) + T0_degC;
            printf("[SNS] Temperature : %.2f C\r\n", g_temperature);
        }
        if (hts_status & 0x01) {
            hts221_humidity_raw_get(&dev_ctx_hts221, &raw_humidity);
            g_humidity = (raw_humidity - H0_T0_out) * (H1_rh - H0_rh) / (H1_T0_out - H0_T0_out) + H0_rh;
            printf("[SNS] Humidite    : %.2f %%\r\n", g_humidity);
        }

        printf("[SNS] ----------------------------------\r\n");

        /* Pousse dans le ring buffer MLP */
        h1_push(g_temperature, g_humidity, g_pressure);

        /* Inférence MLP H+1 (C pur, sans NPU) — mesure précise DWT */
        uint32_t t_infer_start = DWT->CYCCNT;
        H1Result res = h1_infer();
        s_infer_cycles = DWT->CYCCNT - t_infer_start;

        /* Variables pour le clignotement — déclarées avant le if */
        GPIO_TypeDef *blink_port1 = NULL, *blink_port2 = NULL;
        uint16_t      blink_pin1  = 0,     blink_pin2  = 0;

        if (res.ready) {
            const char *name = h1_class_name(res.label);
            strncpy((char *)g_prediction_h1, name, sizeof(g_prediction_h1) - 1);
            g_confidence_h1 = res.confidence;
            printf("[H1] Resume: %s (%.1f%%)  scores: C=%.2f P=%.2f B=%.2f\r\n",
                   name, res.confidence * 100.0f,
                   res.scores[0], res.scores[1], res.scores[2]);

            /* LEDs selon prédiction (actives à l'état bas) */
            /* Éteindre toutes les LEDs d'abord */
            HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_SET);

            if (res.label == H1_CLASS_CLAIR) {
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
                blink_port1 = LED_GREEN_GPIO_Port; blink_pin1 = LED_GREEN_Pin;
            } else if (res.label == H1_CLASS_PLUIE) {
                HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
                blink_port1 = LED_RED_GPIO_Port; blink_pin1 = LED_RED_Pin;
            } else { /* Brouillard → les deux */
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_RESET);
                blink_port1 = LED_GREEN_GPIO_Port; blink_pin1 = LED_GREEN_Pin;
                blink_port2 = LED_RED_GPIO_Port;   blink_pin2 = LED_RED_Pin;
            }
        } else {
            printf("[H1] Resume: historique insuffisant\r\n");
        }

        /* Signale au thread TCP qu'un nouveau cycle capteurs + inference est pret. */
        g_sample_seq++;

        /* Fin de la partie active — capture avant les sleeps */
        s_active_cycles = DWT->CYCCNT - t_active_start;

        /* Cycle 5 s : 4.8 s état stable + bref blink 100 ms */
        tx_thread_sleep(480);
        if (blink_port1) HAL_GPIO_TogglePin(blink_port1, blink_pin1);
        if (blink_port2) HAL_GPIO_TogglePin(blink_port2, blink_pin2);
        tx_thread_sleep(10);
        if (blink_port1) HAL_GPIO_TogglePin(blink_port1, blink_pin1);
        if (blink_port2) HAL_GPIO_TogglePin(blink_port2, blink_pin2);
        tx_thread_sleep(10);
    }
}

/* ============================================================ */
/* RISAF4 + RISAF5 — Déverrouillage accès mémoire pour le NPU  */
/* RISAF4 = NPU_MST0 (0x54029000), RISAF5 = NPU_MST1           */
/* Couvre : AXISRAM2/3 (EC blob + poids) + AXISRAM5 (I/O NPU)  */
/* Doit être appelé AVANT LL_ATON_RT_RuntimeInit()             */
/* ============================================================ */
#ifndef RISAF4_BASE
#define RISAF4_BASE  0x54029000UL
#endif
#ifndef RISAF5_BASE
#define RISAF5_BASE  0x5402A000UL
#endif

typedef struct {
    volatile uint32_t CFGR, STARTR, ENDR, CIDCFGR;
    volatile uint32_t ACFGR, ASTARTR, AENDR, ANESTR;
    volatile uint32_t BCFGR, BSTARTR, BENDR, BNESTR;
    uint32_t reserved[4];  /* pad to 0x40 stride */
} RISAF_REGION_t;

#define RISAF4_REG(x) ((RISAF_REGION_t*)(RISAF4_BASE + 0x040u + 0x40u*((x)-1u)))
#define RISAF5_REG(x) ((RISAF_REGION_t*)(RISAF5_BASE + 0x040u + 0x40u*((x)-1u)))

static void npu_risaf_init(void)
{
    /* Vérifie GLOCK sur RISAF4 (bit 31 du registre CR à offset 0x000) */
    volatile uint32_t *risaf4_cr = (volatile uint32_t *)(RISAF4_BASE + 0x000u);
    if (*risaf4_cr & 0x80000000u) {
        printf("[RIF] RISAF4 GLOCK actif — config ignoree\r\n");
        return;
    }

    /* CID0 (NPU, defaut reset) + CID1 (CPU) : lecture + ecriture */
    uint32_t cid = (1u<<0)|(1u<<1)    /* RDEN CID0, CID1 */
                 | (1u<<16)|(1u<<17); /* WREN CID0, CID1 */
    uint32_t cfg = (1u<<0)|(1u<<8);   /* BREN=1, SEC=1   */

    /* Region 1 : AXISRAM2/3 — EC blob + poids (0x34100000–0x342DFFFF) */
    RISAF4_REG(1)->STARTR  = 0x34100000u;
    RISAF4_REG(1)->ENDR    = 0x342DFFFFu;
    RISAF4_REG(1)->CIDCFGR = cid;
    RISAF4_REG(1)->CFGR    = cfg;

    RISAF5_REG(1)->STARTR  = 0x34100000u;
    RISAF5_REG(1)->ENDR    = 0x342DFFFFu;
    RISAF5_REG(1)->CIDCFGR = cid;
    RISAF5_REG(1)->CFGR    = cfg;

    /* Region 2 : AXISRAM5 — buffers I/O NPU (0x342E0000–0x3434FFFF) */
    RISAF4_REG(2)->STARTR  = 0x342E0000u;
    RISAF4_REG(2)->ENDR    = 0x3434FFFFu;
    RISAF4_REG(2)->CIDCFGR = cid;
    RISAF4_REG(2)->CFGR    = cfg;

    RISAF5_REG(2)->STARTR  = 0x342E0000u;
    RISAF5_REG(2)->ENDR    = 0x3434FFFFu;
    RISAF5_REG(2)->CIDCFGR = cid;
    RISAF5_REG(2)->CFGR    = cfg;

    printf("[RIF] RISAF4+5 configures (NPU_MST0+1 -> AXISRAM2/3/5)\r\n");
}

static void App_Try_Init_Npu(void)
{
    static uint8_t s_npu_init_done = 0;

    if (s_npu_init_done) {
        return;
    }

    npu_risaf_init();
    printf("[NPU] Initialisation runtime...\r\n");
    LL_ATON_RT_RuntimeInit();
    s_npu_init_done = 1;
    printf("[NPU] Runtime initialise\r\n");
}

static VOID App_TCP_Thread_Entry(ULONG thread_input)
{
  UINT ret;
  ULONG bytes_read;
  static UCHAR data_buffer[768];
  NX_PACKET *server_packet;
  NX_PACKET *data_packet;

  /* Renseigne l'IP publique de ton VPS ici */
  ULONG server_ip = IP_ADDRESS(45, 155, 170, 159);
  UINT server_port = 5080;

  static char json_payload[512];
  static char http_request[1024];
  ULONG last_posted_seq = 0;

  ret = nx_tcp_socket_create(&NetXDuoEthIpInstance, &TCPSocket, "TCP Client Socket",
                             NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                             WINDOW_SIZE, NX_NULL, NX_NULL);
  if (ret != NX_SUCCESS) Error_Handler();

  ret = nx_tcp_client_socket_bind(&TCPSocket, NX_ANY_PORT, NX_WAIT_FOREVER);
  if (ret != NX_SUCCESS) Error_Handler();

  /* MX_NetXDuo_Init a fait tx_thread_resume() après IP+GW — réseau prêt */
  printf("TCP thread demarre\r\n");

  /* Laisser l'ARP du gateway se resoudre avant les premiers envois. */
  tx_thread_sleep(500);

  while(1)
  {
    ULONG pending_seq = g_sample_seq;
    if ((pending_seq == 0U) || (pending_seq == last_posted_seq))
    {
      tx_thread_sleep(20);
      continue;
    }

    last_posted_seq = pending_seq;
    printf("[POST] Nouvelle mesure #%lu -> tentative envoi VPS\r\n", pending_seq);

    printf("Tentative connexion VPS 45.155.170.159:5080...\r\n");
    ret = nx_tcp_client_socket_connect(&TCPSocket, server_ip, server_port, 10 * NX_IP_PERIODIC_RATE);
    printf("Connexion ret=0x%04X\r\n", (unsigned)ret);

    if (ret == NX_SUCCESS)
    {
      /* 1. Formatage du JSON */
      int json_len = snprintf(json_payload, sizeof(json_payload),
          "{\"device_id\":\"NUCLEO-N657X0\","
          "\"temperature\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,"
          "\"prediction_h1\":\"%s\",\"confidence_h1\":%.4f,"
          "\"cpu_load\":%.1f,\"infer_time_us\":%.2f,\"power_mw\":%.1f,"
          "\"cycle_ms\":%.0f,\"uptime_s\":%lu,"
          "\"post_status\":\"%s\",\"post_ok_count\":%lu,\"post_fail_count\":%lu}",
          g_temperature, g_humidity, g_pressure,
          (g_prediction_h1[0] ? (const char *)g_prediction_h1 : ""),
          (double)g_confidence_h1,
          (double)g_cpu_load_pct,
          (double)g_infer_time_us,
          (double)g_power_mw,
          (double)g_cycle_ms,
          g_uptime_s,
          (const char *)g_post_status,
          g_post_ok_count,
          g_post_fail_count);

      /* 2. Formatage de la requête HTTP brute (Le Content-Length dynamique est crucial) */
      int req_len = snprintf(http_request, sizeof(http_request),
          "POST /api/data HTTP/1.1\r\n"
          "Host: atmosai.willydev.xyz\r\n"
          "Content-Type: application/json\r\n"
          "X-API-Key: atmosai_w1lly_2026\r\n"
          "Content-Length: %d\r\n"
          "Connection: close\r\n\r\n"
          "%s",
          json_len, json_payload);

      /* 3. Allocation et envoi */
      ret = nx_packet_allocate(&NxAppPool, &data_packet, NX_TCP_PACKET, TX_WAIT_FOREVER);
      if (ret == NX_SUCCESS)
      {
        nx_packet_data_append(data_packet, (VOID *)http_request, req_len, &NxAppPool, TX_WAIT_FOREVER);
        nx_tcp_socket_send(&TCPSocket, data_packet, DEFAULT_TIMEOUT);

        /* 4. Lecture de la réponse de Flask */
        ret = nx_tcp_socket_receive(&TCPSocket, &server_packet, DEFAULT_TIMEOUT);
        if (ret == NX_SUCCESS)
        {
          nx_packet_data_retrieve(server_packet, data_buffer, &bytes_read);
          data_buffer[bytes_read] = '\0';
          /* Vérifie juste le code HTTP (201 = succès) */
          if (strstr((char*)data_buffer, "201") != NULL)
          {
              strncpy((char*)g_post_status, "ok", sizeof(g_post_status) - 1);
              ((char*)g_post_status)[sizeof(g_post_status) - 1] = '\0';
              g_post_ok_count++;
              printf("[POST] OK 201 -> VPS\r\n");
          }
          else
          {
              strncpy((char*)g_post_status, "bad_reply", sizeof(g_post_status) - 1);
              ((char*)g_post_status)[sizeof(g_post_status) - 1] = '\0';
              g_post_fail_count++;
              printf("[POST] Reponse inattendue : %.40s\r\n", data_buffer);
          }
          nx_packet_release(server_packet);
        }
        else
        {
          strncpy((char*)g_post_status, "no_reply", sizeof(g_post_status) - 1);
          ((char*)g_post_status)[sizeof(g_post_status) - 1] = '\0';
          g_post_fail_count++;
        }
      }
      else
      {
        strncpy((char*)g_post_status, "alloc_fail", sizeof(g_post_status) - 1);
        ((char*)g_post_status)[sizeof(g_post_status) - 1] = '\0';
        g_post_fail_count++;
      }

      /* Déconnexion propre après le POST */
      nx_tcp_socket_disconnect(&TCPSocket, DEFAULT_TIMEOUT);

      /* ── GET /api/command — downlink VPS → carte ── */
      int get_len = snprintf(http_request, sizeof(http_request),
          "GET /api/command HTTP/1.1\r\n"
          "Host: atmosai.willydev.xyz\r\n"
          "X-API-Key: atmosai_w1lly_2026\r\n"
          "Connection: close\r\n\r\n");

      ret = nx_tcp_client_socket_connect(&TCPSocket, server_ip, server_port, 10 * NX_IP_PERIODIC_RATE);
      if (ret == NX_SUCCESS)
      {
        ret = nx_packet_allocate(&NxAppPool, &data_packet, NX_TCP_PACKET, TX_WAIT_FOREVER);
        if (ret == NX_SUCCESS)
        {
          nx_packet_data_append(data_packet, (VOID*)http_request, get_len, &NxAppPool, TX_WAIT_FOREVER);
          nx_tcp_socket_send(&TCPSocket, data_packet, DEFAULT_TIMEOUT);

          ret = nx_tcp_socket_receive(&TCPSocket, &server_packet, DEFAULT_TIMEOUT);
          if (ret == NX_SUCCESS)
          {
            nx_packet_data_retrieve(server_packet, data_buffer, &bytes_read);
            data_buffer[bytes_read] = '\0';

            /* Parse "cmd":"..." dans la réponse JSON */
            char *p = strstr((char*)data_buffer, "\"cmd\"");
            if (p) {
              p = strchr(p, ':');
              if (p) {
                p++; while (*p == ' ' || *p == '\t') p++;
                if (*p == '"') {
                  p++;
                  char *end = strchr(p, '"');
                  if (end) {
                    int len = (int)(end - p);
                    if (len > 0 && len < (int)sizeof(g_server_cmd)) {
                      strncpy((char*)g_server_cmd, p, len);
                      ((char*)g_server_cmd)[len] = '\0';
                      if (strcmp((char*)g_server_cmd, "none") != 0)
                        printf("[CMD] Commande recue du VPS : %s\r\n", (char*)g_server_cmd);
                    }
                  }
                }
              }
            }

            /* Parse "hour" et "month" — synchronisation heure VPS */
            int vps_hour = -1, vps_month = -1;
            char *ph = strstr((char*)data_buffer, "\"hour\"");
            if (ph) { ph = strchr(ph, ':'); if (ph) sscanf(ph + 1, " %d", &vps_hour); }
            char *pm = strstr((char*)data_buffer, "\"month\"");
            if (pm) { pm = strchr(pm, ':'); if (pm) sscanf(pm + 1, " %d", &vps_month); }
            if (vps_hour >= 0 && vps_month >= 1) {
              h1_set_time(vps_hour, vps_month);
              printf("[TIME] %02dh UTC  mois %d\r\n", vps_hour, vps_month);
            }
            nx_packet_release(server_packet);
          }
        }
        nx_tcp_socket_disconnect(&TCPSocket, DEFAULT_TIMEOUT);
      }
    }
    else
    {
      strncpy((char*)g_post_status, "connect_fail", sizeof(g_post_status) - 1);
      ((char*)g_post_status)[sizeof(g_post_status) - 1] = '\0';
      g_post_fail_count++;
      printf("Erreur de connexion TCP : 0x%02X\r\n", ret);
    }

    tx_thread_sleep(20);
  }
}

static VOID App_Link_Thread_Entry(ULONG thread_input)
{
  ULONG actual_status;
  UINT linkdown = 0, status;

  while(1)
  {
    /* Send request to check if the Ethernet cable is connected. */
    status = nx_ip_interface_status_check(&NetXDuoEthIpInstance, 0, NX_IP_LINK_ENABLED,
                                      &actual_status, 10);

    if(status == NX_SUCCESS)
    {
      if(linkdown == 1)
      {
        linkdown = 0;

        /* The network cable is connected. */
        printf("The network cable is connected.\n");

        /* Send request to enable PHY Link. */
        nx_ip_driver_direct_command(&NetXDuoEthIpInstance, NX_LINK_ENABLE,
                                      &actual_status);

        /* Send request to check if an address is resolved. */
        status = nx_ip_interface_status_check(&NetXDuoEthIpInstance, 0, NX_IP_ADDRESS_RESOLVED,
                                      &actual_status, 10);
        if(status == NX_SUCCESS)
        {
          /* Stop DHCP */
          nx_dhcp_stop(&DHCPClient);

          /* Reinitialize DHCP */
          nx_dhcp_reinitialize(&DHCPClient);

          /* Start DHCP */
          nx_dhcp_start(&DHCPClient);

          /* wait until an IP address is ready */
          if(tx_semaphore_get(&DHCPSemaphore, TX_WAIT_FOREVER) != TX_SUCCESS)
          {
            /* USER CODE BEGIN DHCPSemaphore get error */
            Error_Handler();
            /* USER CODE END DHCPSemaphore get error */
          }

          PRINT_IP_ADDRESS(IpAddress);

        }
        else
        {
          /* Set the DHCP Client's remaining lease time to 0 seconds to trigger an immediate renewal request for a DHCP address. */
          nx_dhcp_client_update_time_remaining(&DHCPClient, 0);
        }
      }
    }
    else
    {
      if(0 == linkdown)
      {
        linkdown = 1;
        /* The network cable is not connected. */
        printf("The network cable is not connected.\n");
        nx_ip_driver_direct_command(&NetXDuoEthIpInstance, NX_LINK_DISABLE,
                                      &actual_status);
      }
    }

    tx_thread_sleep(NX_APP_CABLE_CONNECTION_CHECK_PERIOD);
  }
}

/* USER CODE END 1 */
