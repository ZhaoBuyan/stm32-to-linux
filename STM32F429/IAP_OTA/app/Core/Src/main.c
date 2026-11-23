/**
 ******************************************************************************
 * @file    main.c
 * @brief   APP 业务程序（0x08020000，896 KB）
 *
 * 两个版本共用这一份源码，靠宏 APP_V11 区分：
 *
 *          采样周期    LED       上报字段
 *   v1.0   500 ms     慢闪      时间 + 模拟值
 *   v1.1   200 ms     快闪      时间 + 模拟值 + 版本号 + 故障计数
 *
 * 这样"升级成功"是能被**观察到**的：采样变快、LED 变快、串口多两个字段。
 * 任务书第 15 行专门强调不能只改版本字符串。
 *
 * ============================ 三个必须注意的点 ============================
 *
 * ① SCB->VTOR 必须在**任何中断使能之前**设置（所以放在 main 第一句）
 *    HAL_Init() 内部会配 SysTick，如果 VTOR 还指向 Bootloader，
 *    SysTick 中断就会跳到 Bootloader 的向量表 —— 症状是"程序能跑但时基全乱"。
 *
 * ② 链接地址必须是 0x08020000（Keil IROM1），否则取指全靠运气
 *
 * ③ 进入维护模式的请求要能穿过复位
 *    APP 写 BKP0R 一个魔数然后软复位，Bootloader 开机读它。
 *    RAM 不能用来传，因为复位后 RAM 内容不保证。
 ******************************************************************************
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f4xx_hal.h"

#include "app_layout.h"
#include "version.h"

UART_HandleTypeDef huart1;

/* Bootloader 与 APP 约定的魔数（备份寄存器）*/
#define BOOT_REQ_MAGIC   0xB0071A70u
#define RTC_BKP0_REG     (*(volatile uint32_t *)0x40002850u)

/* ============================================================================
 * 串口输出
 * ========================================================================== */
static void uart_puts(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 1000);
}

static void uart_printf(const char *fmt, ...)
{
    char    buf[192];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0)
    {
        if ((size_t)n > sizeof(buf) - 1u) { n = (int)(sizeof(buf) - 1u); }
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 1000);
    }
}

/* ============================================================================
 * 时钟与引脚
 * ========================================================================== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 15;
    osc.PLL.PLLN       = 216;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;      /* 180 MHz */
    osc.PLL.PLLQ       = 4;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { for (;;) { } }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) { for (;;) { } }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* KEY1 = PA0（下拉），KEY2 = PC13（下拉）*/
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;

    g.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &g);

    /* RGB LED，共阳极低电平亮 */
    g.Pin   = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &g);

    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_SET);
}

static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) { for (;;) { } }
}

/* ============================================================================
 * 请求进入 Bootloader（写备份寄存器 + 软复位）
 * ========================================================================== */
static void request_bootloader(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();               /* 访问 BKP 寄存器需要 RTC APB 时钟 */
    HAL_PWR_EnableBkUpAccess();

    RTC_BKP0_REG = BOOT_REQ_MAGIC;

    uart_puts("\r\n>>> maintenance requested, resetting...\r\n");
    HAL_Delay(50);

    NVIC_SystemReset();
}

/* ============================================================================
 * 模拟环境采样（后续可以换成真实传感器）
 *
 * 用一个会随时间和温度缓慢漂移的伪随机值，让 v1.0/v1.1 的输出
 * 有可见的节奏差异（500ms vs 200ms 各出一个点）。
 * ========================================================================== */
static uint16_t sample_fake_sensor(uint32_t tick)
{
    /* 简单线性同余 + 正弦近似，保证每次输出都不一样但幅度合理 */
    uint32_t x = tick * 2654435761u;

    x ^= x >> 13;
    x *= 0x85EBCA6Bu;
    x ^= x >> 16;

    /* 落在 200..800 之间，模拟"环境值" */
    return (uint16_t)(200u + (x % 600u));
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    uint32_t next_sample = 0;
    uint32_t next_led    = 0;
    uint32_t ticks       = 0;
    uint8_t  led_state   = 0;
    uint8_t  b;

    /* ① 第一件事：修正向量表（必须在任何中断使能之前）*/
    SCB->VTOR = APP_ADDR;

    /* ② 备份域里有没有维护请求？*/
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* ③ HAL 初始化（会配 SysTick，此时 VTOR 已经对了）*/
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    uart_puts("\r\n\r\n");
    Version_PrintBanner();
    uart_printf("APP base   : 0x%08lX\r\n", (unsigned long)APP_ADDR);
    uart_printf("VTOR       : 0x%08lX\r\n", (unsigned long)SCB->VTOR);

    /* ④ 启动时按住 KEY2 -> 请求进 Bootloader */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET)
    {
        uart_puts("KEY2 held at boot\r\n");
        request_bootloader();
    }

    uart_puts("business running. send 'B' over UART anytime to request maintenance.\r\n\r\n");

    next_sample = HAL_GetTick();
    next_led    = HAL_GetTick();

    for (;;)
    {
        ticks = HAL_GetTick();

        /* ---------- 业务采样 ---------- */
        if ((int32_t)(ticks - next_sample) >= 0)
        {
            uint16_t v = sample_fake_sensor(ticks);
            next_sample = ticks + FW_SAMPLE_MS;

            /* v1.1 多报两个字段：版本号、故障计数 */
#ifdef APP_V11
            uart_printf("[APP v%s] t=%lu env=%u faults=%lu\r\n",
                        FW_VERSION_STR, (unsigned long)ticks,
                        (unsigned)v, (unsigned long)Version_FaultCount());
#else
            uart_printf("[APP v%s] t=%lu env=%u\r\n",
                        FW_VERSION_STR, (unsigned long)ticks, (unsigned)v);
#endif
        }

        /* ---------- LED 心跳（v1.0 慢、v1.1 快）---------- */
        if ((int32_t)(ticks - next_led) >= 0)
        {
            next_led = ticks + FW_LED_TOGGLE_MS;
            led_state = (uint8_t)(!led_state);

            if (led_state)
            {
                HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_RESET);  /* 绿亮 */
            }
            else
            {
                HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_SET);
            }
        }

        /* ---------- 串口命令：'B' 请求维护 ---------- */
        if (HAL_UART_Receive(&huart1, &b, 1u, 0u) == HAL_OK)
        {
            if (b == 'B' || b == 'b')
            {
                request_bootloader();
            }
            else if (b == 'V' || b == 'v')
            {
                uart_printf("version %s (%s %s), faults %lu\r\n",
                            FW_VERSION_STR, __DATE__, __TIME__,
                            (unsigned long)Version_FaultCount());
            }
        }
    }
}

/* ============================================================================
 * HAL 回调
 * ========================================================================== */
void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
    for (;;) { }
}
#endif
