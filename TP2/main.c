/* =============================================================
 * TP2 — Prise en main STM32N657X0 : LED, capteurs, Ethernet
 * ETRS606 — IA Embarquée · Université Savoie Mont Blanc
 * ============================================================= */

#include "main.h"
#include <stdio.h>

I2C_HandleTypeDef  hi2c1;
UART_HandleTypeDef huart1;

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();

    printf("TP2 — STM32N657X0 prise en main\r\n");
    printf("LED Blink + init capteurs HTS221/LPS22HH\r\n\r\n");

    /* Vérification I2C — ping HTS221 */
    if (HAL_I2C_IsDeviceReady(&hi2c1, 0xBE, 3, 100) == HAL_OK)
        printf("HTS221 detecte sur I2C\r\n");
    else
        printf("HTS221 non detecte\r\n");

    /* Vérification I2C — ping LPS22HH */
    if (HAL_I2C_IsDeviceReady(&hi2c1, 0xBA, 3, 100) == HAL_OK)
        printf("LPS22HH detecte sur I2C\r\n");
    else
        printf("LPS22HH non detecte\r\n");

    printf("\r\nBlink...\r\n");

    uint32_t count = 0;
    while (1)
    {
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
        HAL_Delay(500);
        HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
        HAL_Delay(500);

        if (++count % 10 == 0)
            printf("Tick %lu — LEDs OK\r\n", (unsigned long)count);
    }
}

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
