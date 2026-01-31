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
#include "stm32h5xx_hal.h"

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
void MX_USB_HCD_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Z80_RESET_Pin GPIO_PIN_13
#define Z80_RESET_GPIO_Port GPIOC
#define Z80_MEMRQ_Pin GPIO_PIN_14
#define Z80_MEMRQ_GPIO_Port GPIOC
#define Z80_WR_Pin GPIO_PIN_15
#define Z80_WR_GPIO_Port GPIOC
#define Z80_A0_Pin GPIO_PIN_0
#define Z80_A0_GPIO_Port GPIOC
#define Z80_A1_Pin GPIO_PIN_1
#define Z80_A1_GPIO_Port GPIOC
#define Z80_A2_Pin GPIO_PIN_2
#define Z80_A2_GPIO_Port GPIOC
#define Z80_A3_Pin GPIO_PIN_3
#define Z80_A3_GPIO_Port GPIOC
#define Z80_IOREQ_Pin GPIO_PIN_1
#define Z80_IOREQ_GPIO_Port GPIOA
#define USB_FS_PWR_EN_Pin GPIO_PIN_4
#define USB_FS_PWR_EN_GPIO_Port GPIOA
#define BANK_SEL0_Pin GPIO_PIN_5
#define BANK_SEL0_GPIO_Port GPIOA
#define BANK_SEL1_Pin GPIO_PIN_6
#define BANK_SEL1_GPIO_Port GPIOA
#define Z80_INT_Pin GPIO_PIN_7
#define Z80_INT_GPIO_Port GPIOA
#define Z80_A4_Pin GPIO_PIN_4
#define Z80_A4_GPIO_Port GPIOC
#define Z80_A5_Pin GPIO_PIN_5
#define Z80_A5_GPIO_Port GPIOC
#define Z80_D0_Pin GPIO_PIN_0
#define Z80_D0_GPIO_Port GPIOB
#define Z80_D1_Pin GPIO_PIN_1
#define Z80_D1_GPIO_Port GPIOB
#define Z80_D2_Pin GPIO_PIN_2
#define Z80_D2_GPIO_Port GPIOB
#define Z80_A13_Pin GPIO_PIN_13
#define Z80_A13_GPIO_Port GPIOB
#define Z80_A14_Pin GPIO_PIN_14
#define Z80_A14_GPIO_Port GPIOB
#define Z80_A15_Pin GPIO_PIN_15
#define Z80_A15_GPIO_Port GPIOB
#define Z80_A6_Pin GPIO_PIN_6
#define Z80_A6_GPIO_Port GPIOC
#define Z80_A7_Pin GPIO_PIN_7
#define Z80_A7_GPIO_Port GPIOC
#define Z80_A8_Pin GPIO_PIN_8
#define Z80_A8_GPIO_Port GPIOC
#define Z80_A9_Pin GPIO_PIN_9
#define Z80_A9_GPIO_Port GPIOC
#define Z80_WAIT_Pin GPIO_PIN_8
#define Z80_WAIT_GPIO_Port GPIOA
#define SPI1_SS_Pin GPIO_PIN_9
#define SPI1_SS_GPIO_Port GPIOA
#define Z80_PWR_EN_Pin GPIO_PIN_10
#define Z80_PWR_EN_GPIO_Port GPIOA
#define Z80_NMI_Pin GPIO_PIN_15
#define Z80_NMI_GPIO_Port GPIOA
#define Z80_A10_Pin GPIO_PIN_10
#define Z80_A10_GPIO_Port GPIOC
#define Z80_A11_Pin GPIO_PIN_11
#define Z80_A11_GPIO_Port GPIOC
#define Z80_A12_Pin GPIO_PIN_12
#define Z80_A12_GPIO_Port GPIOC
#define Z80_BUSRQ_Pin GPIO_PIN_2
#define Z80_BUSRQ_GPIO_Port GPIOD
#define Z80_D3_Pin GPIO_PIN_3
#define Z80_D3_GPIO_Port GPIOB
#define Z80_D4_Pin GPIO_PIN_4
#define Z80_D4_GPIO_Port GPIOB
#define Z80_D5_Pin GPIO_PIN_5
#define Z80_D5_GPIO_Port GPIOB
#define Z80_D6_Pin GPIO_PIN_6
#define Z80_D6_GPIO_Port GPIOB
#define Z80_D7_Pin GPIO_PIN_7
#define Z80_D7_GPIO_Port GPIOB
#define Z80_RD_Pin GPIO_PIN_8
#define Z80_RD_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
