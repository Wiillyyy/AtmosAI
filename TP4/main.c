/* =============================================================
 * TP4 — Inférence IA embarquée sur STM32N657X0
 * ETRS606 — IA Embarquée · Université Savoie Mont Blanc
 *
 * Objectif TP4 :
 *   - Intégrer le modèle MLP entraîné sous Python/TensorFlow
 *   - Déployer via X-CUBE-AI (conversion TF → C)
 *   - Lancer l'inférence H+1 localement sur le microcontrôleur
 *   - Comparer Edge (STM32) vs Cloud (VPS Keras J+1/J+2/J+3)
 *
 * Évolution par rapport au TP3 :
 *   - Ajout du ring buffer d'historique (560 échantillons, ~3h)
 *   - Calcul de 13 features en temps réel (deltas, cycliques, Magnus)
 *   - Appel h1_infer() : MLP C float32 — < 1 ms, < 1% CPU
 *   - Envoi de prediction_h1 + confidence_h1 dans le JSON
 *   - LEDs selon la prédiction (vert = Clair, rouge = Pluie)
 *
 * Note sur le NPU :
 *   L'intégration ATON (NPU hardware) a été tentée mais abandonnée.
 *   AXISRAM5 (0x342e0000) est inaccessible au CPU → BusFault.
 *   Le modèle X-CUBE-AI étant pure_sw, h1_infer() est équivalent.
 * ============================================================= */

#include "main.h"
#include "app_netxduo.h"
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

    printf("\r\n");
    printf("  TP4 — IA Embarquee sur STM32N657X0\r\n");
    printf("  MLP H+1 : 13 features → 3 classes\r\n");
    printf("  Inference locale, sans cloud, < 1 ms\r\n\r\n");

    MX_NetXDuo_Init(NULL);

    tx_kernel_enter();

    while (1) { /* unreachable */ }
}

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
