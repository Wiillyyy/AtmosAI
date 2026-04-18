/* =============================================================
 * TP3 — Station météo embarquée : acquisition capteurs + envoi réseau
 * ETRS606 — IA Embarquée · Université Savoie Mont Blanc
 *
 * Objectif TP3 :
 *   - Lire température, humidité, pression (HTS221 + LPS22HH via I2C)
 *   - Obtenir une adresse IP par DHCP (NetXDuo / Azure RTOS)
 *   - Envoyer les mesures en HTTP POST vers notre API Flask (VPS)
 *   - Afficher les valeurs en temps réel sur UART
 *
 * Note : pas encore d'IA embarquée à ce stade.
 *        L'inférence MLP H+1 sera ajoutée en TP4.
 * ============================================================= */

#include "main.h"
#include "app_netxduo.h"
#include <stdio.h>

/* Handle I2C partagé avec les drivers capteurs */
I2C_HandleTypeDef hi2c1;

/* UART pour le debug */
UART_HandleTypeDef huart1;

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Init périphériques */
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USART1_UART_Init();

    printf("\r\n=== TP3 — Station meteo embarquee ===\r\n");
    printf("STM32N657X0 | Azure RTOS | NetXDuo\r\n");
    printf("Demarrage...\r\n\r\n");

    /* Démarrage du stack NetXDuo + ThreadX
     * Tout le code applicatif est dans app_netxduo.c */
    MX_NetXDuo_Init(NULL);

    /* ThreadX prend la main — on ne revient pas ici */
    tx_kernel_enter();

    while (1) { /* unreachable */ }
}

/* Redirige printf vers UART1 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
