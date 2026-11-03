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
#include "oled.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 迟滞回差（单位：光强的 1%，即 0~100 里的 2 格）
 * 含义：进入报警和解除报警用不同的门槛，中间这条窄带里保持原状态不动。 */
#define ALARM_HYST   2u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ============================================================================
 * TIM6 的 10ms 节拍计数
 *
 * 为什么是 volatile：
 *   它被【中断】里改、被【主循环】读。不加 volatile 的话，编译器看到主循环里
 *   没人改过它，可能把值缓存进寄存器 → 主循环永远读到旧值 → 一次都不采样。
 *
 * 为什么叫 tick10：
 *   每次中断代表 10ms，所以"20 个 tick" = 200ms。
 * ========================================================================== */
volatile uint32_t g_tick10 = 0;

/* ============================================================================
 * ADC 的 DMA 缓冲区
 *
 *   两个 uint16_t，对应 ADC 扫描的两个 Rank：
 *     adc_buf[0] ← Rank1 (IN13, PC3 电位器)
 *     adc_buf[1] ← Rank2 (IN8,  PB0 传感器)
 *
 * 为什么是 uint16_t 而不是 uint8_t：
 *   ADC 是 12 位，值域 0~4095，1 个字节装不下，必须 2 字节。
 *
 * 为什么要 volatile：
 *   它被 DMA（硬件）改、被主循环读。不加的话编译器可能把值缓存进寄存器，
 *   主循环就永远读到开机时的 0。
 * ========================================================================== */
volatile uint16_t adc_buf[2] = { 0u, 0u };

/* 报警灯：板载 RGB 红灯 PH10。
 * 共阳极接法 → 输出低电平 = 点亮，输出高电平 = 熄灭。
 * 这里自己初始化，不依赖 CubeMX 里有没有配这个脚。 */
#define ALARM_LED_PORT   GPIOH
#define ALARM_LED_PIN    GPIO_PIN_10

static void LED_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    g.Pin   = ALARM_LED_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ALARM_LED_PORT, &g);

    HAL_GPIO_WritePin(ALARM_LED_PORT, ALARM_LED_PIN, GPIO_PIN_SET);   /* 上电默认灭 */
}

static void LED_Set(uint8_t on)
{
    /* 共阳极：on=1 要拉低，on=0 要拉高 */
    HAL_GPIO_WritePin(ALARM_LED_PORT, ALARM_LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
    LED_Init();
    OLED_Init();
    OLED_ShowStr(0, 0, "ADC Monitor", OLED_SIZE_8X16);

    /* 启动 TIM6 的 10ms 中断节拍（注意 IT：中断方式，不是纯硬件启动） */
    HAL_TIM_Base_Start_IT(&htim6);

    /* ===================== 启动 ADC + DMA（只调这一次！）=====================
     * 这一行之后，ADC 和 DMA 就自己跑起来了：
     *   ADC 连续转换 → 每转完 2 个（两路）就通知 DMA → DMA 搬进 adc_buf
     *   → 因为是 Circular 模式，搬完自动回到开头继续，永不停
     *
     * 主循环里再也不需要 HAL_ADC_Start / Poll。
     * ====================================================================== */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, 2);

    /* 关掉 DMA 的"传输完成"中断。
     * CubeMX 的 NVIC 里那一项锁死不让取消，但寄存器层面可以屏蔽。
     * 我们要的是"安静地搬"——搬完不需要叫 CPU。 */
    __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_TC);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    static uint32_t last  = 0;
    static uint8_t  alarm = 0;      /* 报警状态：必须跨轮次保持，迟滞才有意义 */

    /* 改成看 TIM6 的节拍：20 tick × 10ms = 200ms
     * 无符号相减天然能处理回绕，不用担心计数器溢出 */
    if (g_tick10 - last >= 20)
    {
        char     buf[48];
        uint32_t potRaw = 0, senRaw = 0;
        uint32_t potMv  = 0, senMv  = 0;
        uint32_t lux    = 0;
        uint32_t thresh = 0;

        last = g_tick10;

        /* ===================== DMA 版本 =====================
         * 主循环里【不再启动转换、不再等待】——只读数组。
         *
         *   adc_buf[0] = Rank1 = IN13 = PC3 电位器
         *   adc_buf[1] = Rank2 = IN8  = PB0 传感器
         *
         * DMA 在后台不停地搬，我们每次过来拿最新的两份就行。
         * ================================================== */
        potRaw = adc_buf[0];
        senRaw = adc_buf[1];

        /* 换算：电压(mV) = 原始值 × 3300 / 4095 */
        potMv = (potRaw * 3300u) / 4095u;
        senMv = (senRaw * 3300u) / 4095u;

        /* ===================== 任务三：可调阈值报警 =====================
         * 两路都换算到「0~100 的同一把尺子」再比：
         *   thresh —— 电位器拧出来的报警门槛
         *   lux    —— 当前光强（越亮越大）
         * 都换算之后再比较，逻辑才直观：光越亮越容易超线。
         * ============================================================= */
        thresh = (potRaw * 100u) / 4095u;
        lux    = ((4095u - senRaw) * 100u) / 4095u;   /* AO 随亮度下降，所以要取反 */

        /* 可调阈值报警（带迟滞）
         *   进入报警：lux 明确高于门槛（thresh + HYST）
         *   解除报警：lux 明确低于门槛（thresh - HYST）
         *   中间窄带保持原状态，避免阈值附近来回抖动。
         * 注意：迟滞靠"记住上次状态"生效，所以 alarm 必须是 static。 */
        {
            int32_t lo = (int32_t)thresh - (int32_t)ALARM_HYST;
            int32_t hi = (int32_t)thresh + (int32_t)ALARM_HYST;

            if ((int32_t)lux > hi)            /* 明显超过门槛 → 报警 */
            {
                alarm = 1;
            }
            else if ((int32_t)lux < lo)       /* 明显低于门槛 → 解除 */
            {
                alarm = 0;
            }
            /* 落在 thresh±HYST 这条窄带里：保持上一次的状态，不动 */
        }
        LED_Set(alarm);                               /* 报警则点亮板载红灯 */

        /* ---- OLED 显示 ---- */
        sprintf(buf, "TH :%3lu%%", (unsigned long)thresh);
        OLED_ShowStr(0, 0, buf, OLED_SIZE_8X16);

        sprintf(buf, "LUX:%3lu%%", (unsigned long)lux);
        OLED_ShowStr(0, 2, buf, OLED_SIZE_8X16);

        /* 状态 + 两路原始值/电压：小字排布，五样信息一屏放下 */
        /* 状态行：顺便把节拍数显示出来 —— 数字在涨就说明 TIM6 中断真的在跑 */
        sprintf(buf, "%s t=%lu",
                alarm ? "ALARM " : "NORMAL",
                (unsigned long)g_tick10);
        OLED_ShowStr(0, 4, buf, OLED_SIZE_6X8);

        sprintf(buf, "P:%4lu(%lu.%02luV)",
                (unsigned long)potRaw,
                (unsigned long)(potMv / 1000u), (unsigned long)((potMv % 1000u) / 10u));
        OLED_ShowStr(0, 6, buf, OLED_SIZE_6X8);

        sprintf(buf, "S:%4lu(%lu.%02luV)",
                (unsigned long)senRaw,
                (unsigned long)(senMv / 1000u), (unsigned long)((senMv % 1000u) / 10u));
        OLED_ShowStr(0, 7, buf, OLED_SIZE_6X8);

        /* ---- 串口同步打印 ---- */
        sprintf(buf, "TH=%lu%%  LUX=%lu%%  %s  P=%lu(%lumV) S=%lu(%lumV)\r\n",
                (unsigned long)thresh, (unsigned long)lux,
                alarm ? "ALARM" : "NORMAL",
                (unsigned long)potRaw, (unsigned long)potMv,
                (unsigned long)senRaw, (unsigned long)senMv);
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)strlen(buf), 100);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 15;
  RCC_OscInitStruct.PLL.PLLN = 216;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_144CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 8999;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 99;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* ============================================================================
 * TIM6 更新中断的回调（HAL 在 TIM6_DAC_IRQHandler 里转调到这里）
 *
 * 中断里只做一件事：把节拍数 +1。别的什么都不干。
 * 采 ADC、刷 OLED 这些活儿全部留给主循环。
 * ========================================================================== */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        g_tick10++;                 /* 每 10ms 加一 */
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
