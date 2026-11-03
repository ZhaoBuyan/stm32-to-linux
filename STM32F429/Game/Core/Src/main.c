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
#include "mpu6050.h"
#include "game.h"
#include "app.h"
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
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ----------------------------------------------------------------------------
 * TIM6 的 1ms 节拍
 *
 * volatile 必须有：中断里改、主循环里读。不加的话编译器可能把它缓存进
 * 寄存器，主循环永远读到旧值，表现为"程序卡住不动"。
 * -------------------------------------------------------------------------- */
volatile uint32_t g_ticks = 0;

/* ----------------------------------------------------------------------------
 * IMU 句柄（全局，方便以后别的模块调用）
 * -------------------------------------------------------------------------- */
MPU6050_HandleTypeDef g_imu;

/* ----------------------------------------------------------------------------
 * 「水平参考」—— 静止校准得到的加速度零偏
 *
 * 为什么必须有：
 *   板子明明放平，实测 ax ≈ 300（不是 0）。这 300 就是器件零偏 + 装配倾斜。
 *   如果直接拿 ax 当倾斜量，游戏里的球会一直往一边漂。
 *   所以先记下"放平时读多少"，之后都用【差值】判断倾斜。
 * -------------------------------------------------------------------------- */
static int16_t g_ax_ref = 0;
static int16_t g_ay_ref = 0;

/* ----------------------------------------------------------------------------
 * 应用上下文（游戏 + 按钮 + 状态机）
 * -------------------------------------------------------------------------- */
App_Ctx g_app;

/* ----------------------------------------------------------------------------
 * 按键边沿检测
 *
 * 怎么做到消抖的：这个函数每 20ms 才被调用一次，所以"连续两次读到按下"
 * 本身就隔了 20ms —— 抖动的毛刺撑不过这个间隔。简单但够用。
 * 按下电平：板子按键是 3.3V + 下拉电阻，所以【按下 = 高电平】。
 * -------------------------------------------------------------------------- */
static uint8_t key_pressed_edge(GPIO_TypeDef *port, uint16_t pin, uint8_t *last)
{
    uint8_t now = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t ev  = (now && !(*last)) ? 1u : 0u;      /* 上升沿 = 刚按下 */
    *last = now;
    return ev;
}

/* ----------------------------------------------------------------------------
 * 采集 N 次加速度并求平均，作为水平参考
 * 调用时必须把板子放平不动
 * -------------------------------------------------------------------------- */
static void IMU_CalibrateLevel(MPU6050_HandleTypeDef *dev, uint16_t samples)
{
    int32_t  sum_x = 0, sum_y = 0;
    uint16_t i;
    MPU6050_RawData_t d;

    if (samples == 0) { samples = 1; }

    for (i = 0; i < samples; i++)
    {
        if (MPU6050_ReadRaw(dev, &d) == MPU6050_OK)
        {
            sum_x += d.ax;
            sum_y += d.ay;
        }
        HAL_Delay(5);
    }

    g_ax_ref = (int16_t)(sum_x / (int32_t)samples);
    g_ay_ref = (int16_t)(sum_y / (int32_t)samples);
}

/* ----------------------------------------------------------------------------
 * RGB LED 控制（共阳极：低电平点亮，所以这里是反的）
 *   bit0 = R, bit1 = G, bit2 = B
 * -------------------------------------------------------------------------- */
static void LED_SetRGB(uint8_t r, uint8_t g, uint8_t b)
{
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, r ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, g ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, b ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ----------------------------------------------------------------------------
 * 串口打印小工具（阻塞发送，调试够用）
 * -------------------------------------------------------------------------- */
static void dbg(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
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
  MX_I2C1_Init();
  MX_TIM6_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
    {
        char     buf[80];
        uint8_t  id  = 0;
        MPU6050_Status st;

        /* ① 先开 1ms 节拍 */
        HAL_TIM_Base_Start_IT(&htim6);

        LED_SetRGB(0, 0, 1);                     /* 蓝灯：正在启动 */

        /* ② 直接读 WHO_AM_I —— 这是"芯片到底在不在"最硬的证据 */
        g_imu.hi2c  = &hi2c1;
        g_imu.addr8 = (uint16_t)(0x68 << 1);     /* 0xD0 */

        if (MPU6050_ReadWhoAmI(&g_imu, &id) == MPU6050_OK)
        {
            sprintf(buf, "[imu] WHO_AM_I = 0x%02X  OK\r\n", (unsigned)id);
            dbg(buf);
        }
        else
        {
            sprintf(buf, "[imu] WHO_AM_I read FAILED (got 0x%02X)\r\n", (unsigned)id);
            dbg(buf);
            dbg("[imu] check: VDD/GND/SCL/SDA wiring, addr 0x68 or 0x69, pull-ups\r\n");
            LED_SetRGB(1, 0, 0);                 /* 红灯：传感器没找到 */
            while (1) { }                        /* 停在这里，方便你看到红灯 */
        }

        /* ③ 完整初始化 */
        st = MPU6050_Init(&g_imu, &hi2c1);
        sprintf(buf, "[imu] Init = %d (0=OK)\r\n", (int)st);
        dbg(buf);

        /* ④ 静止校准：这一刻板子必须放平不动，约 2 秒 */
        dbg("[imu] calibrating, keep the board FLAT and STILL ...\r\n");
        MPU6050_CalibrateGyro(&g_imu, 200);          /* 陀螺零偏 */
        IMU_CalibrateLevel(&g_imu, 200);             /* 加速度水平参考 */
        sprintf(buf, "[imu] gyro bias = (%d, %d, %d)\r\n",
                (int)g_imu.gyro_bias[0], (int)g_imu.gyro_bias[1], (int)g_imu.gyro_bias[2]);
        dbg(buf);
        sprintf(buf, "[imu] level ref = ax:%d ay:%d\r\n",
                (int)g_ax_ref, (int)g_ay_ref);
        dbg(buf);

        LED_SetRGB(0, 1, 0);                     /* 绿灯：初始化完成 */

        /* ⑤ 初始化应用：进入 READY，等玩家点开始 */
        App_Init(&g_app);
        dbg("[app] state = READY.\r\n");
        dbg("[app] KEY1 = primary action (start/pause/resume/next/retry)\r\n");
        dbg("[app] KEY2 = restart current level\r\n\r\n");
    }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    {
        static uint32_t last_ctrl  = 0;
        static uint32_t last_print = 0;
        static uint8_t  key1_last  = 0;
        static uint8_t  key2_last  = 0;
        char             buf[140];
        MPU6050_RawData_t d;

        /* ============ 每 20ms 一轮（50Hz）============ */
        if (g_ticks - last_ctrl >= 20)
        {
            uint32_t dtms = g_ticks - last_ctrl;
            Game_State before;
            last_ctrl = g_ticks;

            /* ---- ① 按键（这里是"输入源"之一，以后触摸也走同一套动作）---- */
            if (key_pressed_edge(KEY1_GPIO_Port, KEY1_Pin, &key1_last))
            {
                /* KEY1 = 主操作：当前状态下第一个按钮 */
                if (g_app.button_count > 0)
                {
                    App_DoAction(&g_app, g_app.buttons[0].action);
                    sprintf(buf, "[key] KEY1 -> %s\r\n",
                            App_StateName(&g_app));
                    dbg(buf);
                }
            }
            if (key_pressed_edge(KEY2_GPIO_Port, KEY2_Pin, &key2_last))
            {
                App_DoAction(&g_app, ACT_RESTART);
                dbg("[key] KEY2 -> RESTART\r\n");
            }

            /* ---- ② 读 IMU 并推进游戏 ---- */
            before = g_app.game.state;
            if (MPU6050_ReadRaw(&g_imu, &d) == MPU6050_OK)
            {
                float tx = (float)((int16_t)(d.ax - g_ax_ref));
                float ty = (float)((int16_t)(d.ay - g_ay_ref));

                App_Update(&g_app, tx, ty, (float)dtms / 1000.0f);
            }

            /* ---- ③ 灯：跟着状态走 ---- */
            if (g_app.game.state != before)
            {
                switch (g_app.game.state)
                {
                case GAME_RUNNING: LED_SetRGB(0, 0, 1); break;  /* 蓝：进行中 */
                case GAME_WIN:     LED_SetRGB(0, 1, 0); break;  /* 绿：赢 */
                case GAME_FAIL:    LED_SetRGB(1, 0, 0); break;  /* 红：输 */
                case GAME_PAUSED:  LED_SetRGB(1, 1, 0); break;  /* 黄：暂停 */
                default:           LED_SetRGB(1, 1, 1); break;  /* 白：READY */
                }
                sprintf(buf, "[state] -> %s (level %u)\r\n",
                        App_StateName(&g_app),
                        (unsigned)(g_app.game.level_index + 1));
                dbg(buf);
            }
        }

        /* ============ 每 200ms 打印一次状态 ============ */
        if (g_ticks - last_print >= 200)
        {
            last_print = g_ticks;

            sprintf(buf, "L%u %s ball=(%4d,%4d) v=(%5d,%5d) t=%lums\r\n",
                    (unsigned)(g_app.game.level_index + 1),
                    App_StateName(&g_app),
                    (int)g_app.game.ball.x, (int)g_app.game.ball.y,
                    (int)g_app.game.ball.vx, (int)g_app.game.ball.vy,
                    (unsigned long)g_app.game.elapsed_ms);
            dbg(buf);
        }
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
  htim6.Init.Period = 9;
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
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOH, LED_R_Pin|LED_G_Pin|LED_B_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : KEY2_Pin */
  GPIO_InitStruct.Pin = KEY2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY1_Pin */
  GPIO_InitStruct.Pin = KEY1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_R_Pin LED_G_Pin LED_B_Pin */
  GPIO_InitStruct.Pin = LED_R_Pin|LED_G_Pin|LED_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief  TIM6 更新中断回调 —— 整个游戏的时间基准
 * @note   里面只做一件事：节拍 +1。绝不在中断里采 IMU 或刷屏。
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        g_ticks++;                  /* 每 1ms 加一 */
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
