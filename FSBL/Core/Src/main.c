/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define CAMERA_I2C_ADDRESS (0x1AU << 1)
#define CAMERA_ID_REGISTER 0x3912U
#define CAMERA_EXPECTED_ID 0x00U
#define CAMERA_RETRY_MS 5000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

static UART_HandleTypeDef log_uart;
static I2C_HandleTypeDef camera_i2c;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static void Log_Init(void);
static void Log_Write(const char *message);
static void Camera_Init(void);
static HAL_StatusTypeDef Camera_LogID(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOG_CLK_ENABLE();
  HAL_GPIO_WritePin(BLINK_LED_GPIO_Port, BLINK_LED_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = BLINK_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BLINK_LED_GPIO_Port, &GPIO_InitStruct);

  Log_Init();
  Log_Write("\r\nchaos-air: B-CAMS-IMX camera ID probe\r\n");
  Camera_Init();
  HAL_StatusTypeDef camera_status = Camera_LogID();
  uint32_t last_camera_attempt = HAL_GetTick();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(BLINK_LED_GPIO_Port, BLINK_LED_Pin);
    HAL_Delay(500U);
    if ((camera_status != HAL_OK) &&
        ((HAL_GetTick() - last_camera_attempt) >= CAMERA_RETRY_MS))
    {
      camera_status = Camera_LogID();
      last_camera_attempt = HAL_GetTick();
    }
  }
  /* USER CODE END 3 */
}
/* USER CODE BEGIN CLK 1 */
/* USER CODE END CLK 1 */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the System Power Supply
  */
  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable HSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes TIMPRE when TIM is used as Systick Clock Source
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_TIM;
  PeriphClkInitStruct.TIMPresSelection = RCC_TIMPRES_DIV1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Get current CPU/System buses clocks configuration and if necessary switch
 to intermediate HSI clock to ensure target clock can be set
  */
  HAL_RCC_GetClockConfig(&RCC_ClkInitStruct);
  if ((RCC_ClkInitStruct.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
     (RCC_ClkInitStruct.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
    RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
    {
      /* Initialization Error */
      Error_Handler();
    }
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_CPUCLK|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_PCLK5
                              |RCC_CLOCKTYPE_PCLK4;
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

static void Log_Init(void)
{
  RCC_PeriphCLKInitTypeDef clock = {0};
  GPIO_InitTypeDef gpio = {0};

  clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOE, &gpio);

  log_uart.Instance = USART1;
  log_uart.Init.BaudRate = 115200U;
  log_uart.Init.WordLength = UART_WORDLENGTH_8B;
  log_uart.Init.StopBits = UART_STOPBITS_1;
  log_uart.Init.Parity = UART_PARITY_NONE;
  log_uart.Init.Mode = UART_MODE_TX_RX;
  log_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  log_uart.Init.OverSampling = UART_OVERSAMPLING_16;
  log_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  log_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  log_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&log_uart) != HAL_OK)
  {
    Error_Handler();
  }
}

static void Log_Write(const char *message)
{
  if (HAL_UART_Transmit(&log_uart, (const uint8_t *)message,
                        (uint16_t)strlen(message), 1000U) != HAL_OK)
  {
    Error_Handler();
  }
}

static void Camera_Init(void)
{
  RCC_PeriphCLKInitTypeDef clock = {0};
  GPIO_InitTypeDef gpio = {0};

  clock.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
  clock.I2c2ClockSelection = RCC_I2C2CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
  {
    Log_Write("ERROR: camera I2C2 clock configuration failed\r\n");
    Error_Handler();
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C2_CLK_ENABLE();

  HAL_GPIO_WritePin(CAMERA_RESET_GPIO_Port, CAMERA_RESET_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAMERA_POWER_GPIO_Port, CAMERA_POWER_Pin, GPIO_PIN_RESET);
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = CAMERA_RESET_Pin;
  HAL_GPIO_Init(CAMERA_RESET_GPIO_Port, &gpio);
  gpio.Pin = CAMERA_POWER_Pin;
  HAL_GPIO_Init(CAMERA_POWER_GPIO_Port, &gpio);

  gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  gpio.Mode = GPIO_MODE_AF_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &gpio);

  camera_i2c.Instance = I2C2;
  /* Conservative standard-mode timing for the 32 MHz PCLK1, analog filter on. */
  camera_i2c.Init.Timing = 0x3092232BU;
  camera_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  camera_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  camera_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  camera_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  camera_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if ((HAL_I2C_Init(&camera_i2c) != HAL_OK) ||
      (HAL_I2CEx_ConfigAnalogFilter(&camera_i2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) ||
      (HAL_I2CEx_ConfigDigitalFilter(&camera_i2c, 0U) != HAL_OK))
  {
    Log_Write("ERROR: camera I2C2 initialization failed\r\n");
    Error_Handler();
  }

  HAL_Delay(100U);
  HAL_GPIO_WritePin(CAMERA_POWER_GPIO_Port, CAMERA_POWER_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(CAMERA_RESET_GPIO_Port, CAMERA_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);
}

static HAL_StatusTypeDef Camera_LogID(void)
{
  uint8_t id;
  char message[160];
  HAL_StatusTypeDef status;

  status = HAL_I2C_Mem_Read(&camera_i2c, CAMERA_I2C_ADDRESS,
                            CAMERA_ID_REGISTER, I2C_MEMADD_SIZE_16BIT,
                            &id, 1U, 100U);
  if (status != HAL_OK)
  {
    (void)snprintf(message, sizeof(message),
                   "ERROR: IMX335 ID read failed: I2C2 addr=0x1A status=%u error=0x%08lX; retry in 5 s\r\n",
                   (unsigned int)status, (unsigned long)HAL_I2C_GetError(&camera_i2c));
    Log_Write(message);
    return status;
  }

  (void)snprintf(message, sizeof(message),
                 "Camera: IMX335, I2C2 addr=0x1A, ID[0x3912]=0x%02X%s\r\n",
                 (unsigned int)id, (id == CAMERA_EXPECTED_ID) ? " (matched)" : " (UNEXPECTED)");
  Log_Write(message);
  if (id != CAMERA_EXPECTED_ID)
  {
    Log_Write("ERROR: expected IMX335 ID 0x00; retry in 5 s\r\n");
    return HAL_ERROR;
  }
  return HAL_OK;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  HAL_GPIO_WritePin(BLINK_LED_GPIO_Port, BLINK_LED_Pin, GPIO_PIN_SET);
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
