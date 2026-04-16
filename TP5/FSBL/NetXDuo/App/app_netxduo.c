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
#include "h1.h"
#include "ll_aton_NN_interface.h"
#include "ll_aton_runtime.h"
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
volatile char  g_server_cmd[16] = "none";   /* Downlink VPS → carte */

static const NN_Interface_TypeDef h1_network_if = {
    .network_name          = "h1",
    .ec_network_init       = LL_ATON_EC_Network_Init_h1,
    .ec_inference_init     = LL_ATON_EC_Inference_Init_h1,
    .input_setter          = LL_ATON_Set_User_Input_Buffer_h1,
    .input_getter          = LL_ATON_Get_User_Input_Buffer_h1,
    .output_setter         = LL_ATON_Set_User_Output_Buffer_h1,
    .output_getter         = LL_ATON_Get_User_Output_Buffer_h1,
    .epoch_block_items     = LL_ATON_EpochBlockItems_h1,
    .output_buffers_info   = LL_ATON_Output_Buffers_Info_h1,
    .input_buffers_info    = LL_ATON_Input_Buffers_Info_h1,
    .internal_buffers_info = LL_ATON_Internal_Buffers_Info_h1,
};
static NN_Instance_TypeDef h1_nn_instance = {.network = &h1_network_if, .exec_state = {0}};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static VOID nx_app_thread_entry (ULONG thread_input);
static VOID ip_address_change_notify_callback(NX_IP *ip_instance, VOID *ptr);
/* USER CODE BEGIN PFP */
static VOID App_TCP_Thread_Entry(ULONG thread_input);
static VOID App_Link_Thread_Entry(ULONG thread_input);
static VOID App_Sensor_Thread_Entry(ULONG thread_input);
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
   printf("Looking for DHCP server ..\n");
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

        printf("Gateway IP configuree : %d.%d.%d.%d\r\n",
               gateway_buf[3], gateway_buf[2], gateway_buf[1], gateway_buf[0]);
    }
    else
    {
        printf("Erreur : Impossible de recuperer la Gateway DHCP\r\n");
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

    printf("Capteurs initialises !\r\n");

/* Initialise le ring buffer d'inférence H+1 */
    h1_init();

    while(1)
    {
        uint8_t lps_status = 0, hts_status = 0;
        uint32_t raw_pressure = 0;
        int16_t raw_temperature = 0, raw_humidity = 0;

        // Pression
        lps22hh_read_reg(&dev_ctx_lps22hh, LPS22HH_STATUS, &lps_status, 1);
        if (lps_status & 0x01) {
            lps22hh_pressure_raw_get(&dev_ctx_lps22hh, &raw_pressure);
            g_pressure = lps22hh_from_lsb_to_hpa(raw_pressure);
            printf("LPS22HH - Pression : %.2f hPa\r\n", g_pressure);
        }

        // Température + Humidité
        hts221_read_reg(&dev_ctx_hts221, HTS221_STATUS_REG, &hts_status, 1);
        if (hts_status & 0x02) {
            hts221_temperature_raw_get(&dev_ctx_hts221, &raw_temperature);
            g_temperature = (raw_temperature - T0_out) * (T1_degC - T0_degC) / (T1_out - T0_out) + T0_degC;
            printf("HTS221 - Temperature : %.2f C\r\n", g_temperature);
        }
        if (hts_status & 0x01) {
            hts221_humidity_raw_get(&dev_ctx_hts221, &raw_humidity);
            g_humidity = (raw_humidity - H0_T0_out) * (H1_rh - H0_rh) / (H1_T0_out - H0_T0_out) + H0_rh;
            printf("HTS221 - Humidite : %.2f %%\r\n", g_humidity);
        }

        printf("----------------------------------\r\n");

        /* Pousse dans le ring buffer MLP */
        h1_push(g_temperature, g_humidity, g_pressure);

        /* Inférence MLP H+1 (C pur, sans NPU) */
        H1Result res = h1_infer();

        /* Variables pour le clignotement — déclarées avant le if */
        GPIO_TypeDef *blink_port1 = NULL, *blink_port2 = NULL;
        uint16_t      blink_pin1  = 0,     blink_pin2  = 0;

        if (res.ready) {
            const char *name = h1_class_name(res.label);
            strncpy((char *)g_prediction_h1, name, sizeof(g_prediction_h1) - 1);
            g_confidence_h1 = res.confidence;
            printf("H+1 : %s (%.1f%%)  scores: C=%.2f P=%.2f B=%.2f\r\n",
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
            printf("H+1 : historique insuffisant\r\n");
        }

        /* ── Mode danse (downlink) ou clignotement normal ── */
        if (strcmp((char*)g_server_cmd, "dance") == 0)
        {
            strncpy((char*)g_server_cmd, "none", sizeof(g_server_cmd));
            printf("[CMD] *** MODE DANSE ACTIVE *** Stroboscope 20s !\r\n");
            /* Stroboscope ~20s : rouge et verte alternent toutes les 80ms */
            for (int d = 0; d < 125; d++) {
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, (d % 2 == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
                HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   (d % 2 == 0) ? GPIO_PIN_SET   : GPIO_PIN_RESET);
                tx_thread_sleep(8);  /* 80ms */
            }
            /* LEDs éteintes après la danse */
            HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_RED_GPIO_Port,   LED_RED_Pin,   GPIO_PIN_SET);
        }
        else
        {
            /* Clignotement toutes les 5s — uniquement les LEDs actives */
            for (int b = 0; b < 4; b++) {
                tx_thread_sleep(480);
                if (blink_port1) HAL_GPIO_TogglePin(blink_port1, blink_pin1);
                if (blink_port2) HAL_GPIO_TogglePin(blink_port2, blink_pin2);
                tx_thread_sleep(20);
                if (blink_port1) HAL_GPIO_TogglePin(blink_port1, blink_pin1);
                if (blink_port2) HAL_GPIO_TogglePin(blink_port2, blink_pin2);
            }
        }
    }
}

static VOID App_TCP_Thread_Entry(ULONG thread_input)
{
  UINT ret;
  ULONG bytes_read;
  static UCHAR data_buffer[512];
  NX_PACKET *server_packet;
  NX_PACKET *data_packet;

  /* Renseigne l'IP publique de ton VPS ici */
  ULONG server_ip = IP_ADDRESS(45, 155, 170, 159);
  UINT server_port = 5080;

  static char json_payload[256];
  static char http_request[512];

  ret = nx_tcp_socket_create(&NetXDuoEthIpInstance, &TCPSocket, "TCP Client Socket",
                             NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                             WINDOW_SIZE, NX_NULL, NX_NULL);
  if (ret != NX_SUCCESS) Error_Handler();

  ret = nx_tcp_client_socket_bind(&TCPSocket, NX_ANY_PORT, NX_WAIT_FOREVER);
  if (ret != NX_SUCCESS) Error_Handler();

  /* MX_NetXDuo_Init a fait tx_thread_resume() après IP+GW — réseau prêt */
  printf("TCP thread demarre\r\n");

  while(1)
  {
    /* Laisser l'ARP du gateway se résoudre avant la première tentative */
    tx_thread_sleep(500);

    printf("Tentative connexion VPS 45.155.170.159:5080...\r\n");
    ret = nx_tcp_client_socket_connect(&TCPSocket, server_ip, server_port, 10 * NX_IP_PERIODIC_RATE);
    printf("Connexion ret=0x%04X\r\n", (unsigned)ret);

    if (ret == NX_SUCCESS)
    {
      /* 1. Formatage du JSON */
      int json_len = snprintf(json_payload, sizeof(json_payload),
          "{\"device_id\":\"nucleo_real\","
          "\"temperature\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,"
          "\"prediction_h1\":\"%s\",\"confidence_h1\":%.4f}",
          g_temperature, g_humidity, g_pressure,
          (g_prediction_h1[0] ? (const char *)g_prediction_h1 : ""),
          (double)g_confidence_h1);

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
              printf("[POST] OK 201 -> VPS\r\n");
          else
              printf("[POST] Reponse inattendue : %.40s\r\n", data_buffer);
          nx_packet_release(server_packet);
        }
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
            nx_packet_release(server_packet);
          }
        }
        nx_tcp_socket_disconnect(&TCPSocket, DEFAULT_TIMEOUT);
      }
    }
    else
    {
      printf("Erreur de connexion TCP : 0x%02X\r\n", ret);
    }

    /* Envoi toutes les 20 secondes */
    tx_thread_sleep(2000);
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
