/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SX1262_NRESET_Pin GPIO_PIN_0
#define SX1262_NRESET_GPIO_Port GPIOC
#define SX1262_NSS_Pin GPIO_PIN_4
#define SX1262_NSS_GPIO_Port GPIOA
#define SX1262_BUSY_Pin GPIO_PIN_0
#define SX1262_BUSY_GPIO_Port GPIOB
#define SX1262_DIO1_Pin GPIO_PIN_1
#define RELAY_Pin GPIO_PIN_10
#ifndef WHEEL_BENCH_MODE
#define WHEEL_BENCH_MODE 1
#endif
#define WHEEL_TACH_Pin GPIO_PIN_7
#define WHEEL_TACH_GPIO_Port GPIOC
#ifndef SOLAR_TELEMETRY
#define SOLAR_TELEMETRY 0
#endif
#ifndef EPS_UART_TELEMETRY
#define EPS_UART_TELEMETRY 1
#endif
#if SOLAR_TELEMETRY && EPS_UART_TELEMETRY
#error "PC5 cannot be used as ADC and USART3 RX at the same time"
#endif
#define EPS_ADC_GPIOC_Pins (GPIO_PIN_4 | GPIO_PIN_5)
#define PANEL_ADC_GPIOC_Pins (GPIO_PIN_4 | GPIO_PIN_2 | GPIO_PIN_3)
#define EPS_ADC_GPIOA_Pins (GPIO_PIN_0 | GPIO_PIN_1)
#ifndef SOLAR_R_TOP_OHM
#define SOLAR_R_TOP_OHM 330000U
#endif
#ifndef SOLAR_R_BOTTOM_OHM
#define SOLAR_R_BOTTOM_OHM 68000U
#endif
#ifndef SOLAR_ADC_REF_MV
#define SOLAR_ADC_REF_MV 3300U
#endif
#define RELAY_GPIO_Port GPIOB
/* Common LOW-trigger relay module. Invert this setting for HIGH-trigger IN. */
#ifndef RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL GPIO_PIN_RESET
#endif
#define RELAY_INACTIVE_LEVEL ((RELAY_ACTIVE_LEVEL == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define SX1262_DIO1_GPIO_Port GPIOB
#define SX1262_DIO1_EXTI_IRQn EXTI1_IRQn

/* USER CODE BEGIN Private defines */
#ifndef SX1262_DIAGNOSTIC_MODE
#define SX1262_DIAGNOSTIC_MODE 1
#endif

/* Payload real P2P; 0 conserva la prueba aislada TEST_LORA. */
#ifndef RADIO_QUATERNION_MODE
#define RADIO_QUATERNION_MODE 1
#endif

#ifndef P2P_LEGACY_Q_PAYLOAD
#define P2P_LEGACY_Q_PAYLOAD 0
#endif


/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
