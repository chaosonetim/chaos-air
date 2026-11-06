#include "main.h"

extern uint32_t _sNSCVeneer;
extern uint32_t _eNSCVeneer;

static void Camera_Clocks(void)
{
  RCC_OscInitTypeDef oscillator = {0};
  RCC_ClkInitTypeDef clock = {0};
  RCC_PeriphCLKInitTypeDef peripheral = {0};

  if ((HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK) ||
      (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK))
  {
    Error_Handler();
  }

  clock.ClockType = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK;
  clock.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
  clock.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  if (HAL_RCC_ClockConfig(&clock) != HAL_OK)
  {
    Error_Handler();
  }
  oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  oscillator.HSIState = RCC_HSI_ON;
  oscillator.HSIDiv = RCC_HSI_DIV1;
  oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  oscillator.PLL1.PLLState = RCC_PLL_ON;
  oscillator.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  oscillator.PLL1.PLLM = 4;
  oscillator.PLL1.PLLN = 75;
  oscillator.PLL1.PLLP1 = 1;
  oscillator.PLL1.PLLP2 = 1;
  if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
  {
    Error_Handler();
  }

  clock.ClockType |= RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                     RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5;
  clock.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  clock.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  clock.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clock.IC1Selection.ClockDivider = 2;
  clock.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clock.IC2Selection.ClockDivider = 3;
  clock.IC6Selection = clock.IC2Selection;
  clock.IC11Selection = clock.IC2Selection;
  clock.AHBCLKDivider = RCC_HCLK_DIV2;
  clock.APB1CLKDivider = RCC_APB1_DIV1;
  clock.APB2CLKDivider = RCC_APB2_DIV1;
  clock.APB4CLKDivider = RCC_APB4_DIV1;
  clock.APB5CLKDivider = RCC_APB5_DIV1;
  if (HAL_RCC_ClockConfig(&clock) != HAL_OK)
  {
    Error_Handler();
  }

  peripheral.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP | RCC_PERIPHCLK_CSI |
                                    RCC_PERIPHCLK_I2C2 | RCC_PERIPHCLK_USART1;
  peripheral.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  peripheral.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  peripheral.ICSelection[RCC_IC17].ClockDivider = 4;
  peripheral.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  peripheral.ICSelection[RCC_IC18].ClockDivider = 60;
  /* Keep control-bus timing independent of the faster system buses. */
  peripheral.I2c2ClockSelection = RCC_I2C2CLKSOURCE_HSI;
  peripheral.Usart1ClockSelection = RCC_USART1CLKSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&peripheral) != HAL_OK)
  {
    Error_Handler();
  }
}

void Camera_BootSetup(void)
{
  static const uint32_t peripherals[] = {
    RIF_RISC_PERIPH_INDEX_I2C2, RIF_RISC_PERIPH_INDEX_USART1,
    RIF_RISC_PERIPH_INDEX_TIM6, RIF_RISC_PERIPH_INDEX_CSI,
    RIF_RISC_PERIPH_INDEX_DCMIPP
  };
  RIMC_MasterConfig_t master = {RIF_CID_1, RIF_ATTRIBUTE_NSEC | RIF_ATTRIBUTE_PRIV};
  RISAF_BaseRegionConfig_t memory = {0};

  Camera_Clocks();
  __HAL_RCC_RIFSC_CLK_ENABLE();
  HAL_RIF_RIMC_SetDebugAccessPortCID(RIF_CID_1);
  for (uint32_t i = 0; i < sizeof(peripherals) / sizeof(peripherals[0]); ++i)
  {
    HAL_RIF_RISC_SetSlaveSecureAttributes(peripherals[i], RIF_ATTRIBUTE_NSEC);
  }
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &master);

  memory.Filtering = RISAF_FILTER_ENABLE;
  memory.Secure = RIF_ATTRIBUTE_NSEC;
  memory.ReadWhitelist = RIF_CID_1;
  memory.WriteWhitelist = RIF_CID_1;
  memory.StartAddress = 0;
  memory.EndAddress = SRAM2_AXI_SIZE - 1U;
  HAL_RIF_RISAF_ConfigBaseRegion(RISAF3, RISAF_REGION_1, &memory);

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  HAL_GPIO_ConfigPinAttributes(GPIOA, GPIO_PIN_0, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOB, GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_NSEC);
  HAL_GPIO_ConfigPinAttributes(GPIOO, GPIO_PIN_5, GPIO_PIN_NSEC);

  SAU->CTRL = 0;
  SAU->RNR = 0;
  SAU->RBAR = 0x24100000U;
  SAU->RLAR = 0x241FFFE0U | SAU_RLAR_ENABLE_Msk;
  SAU->RNR = 1;
  SAU->RBAR = 0x40000000U;
  SAU->RLAR = 0x4FFFFFE0U | SAU_RLAR_ENABLE_Msk;
  SAU->RNR = 2;
  SAU->RBAR = (uint32_t)(uintptr_t)&_sNSCVeneer;
  SAU->RLAR = (((uint32_t)(uintptr_t)&_eNSCVeneer - 1U) & SAU_RLAR_LADDR_Msk) |
              SAU_RLAR_ENABLE_Msk | SAU_RLAR_NSC_Msk;
  SAU->CTRL = SAU_CTRL_ENABLE_Msk;
  SCB->NSACR |= (3UL << 10);
  SCB_NS->CPACR |= (3UL << 20) | (3UL << 22);
  NVIC_SetTargetState(TIM6_IRQn);
  NVIC_SetTargetState(DCMIPP_IRQn);
  NVIC_SetTargetState(CSI_IRQn);
  __DSB();
  __ISB();
}
