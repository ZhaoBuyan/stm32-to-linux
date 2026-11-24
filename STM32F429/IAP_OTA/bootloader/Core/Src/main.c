/**
 ******************************************************************************
 * @file    main.c
 * @brief   Bootloader 主程序（0x08000000，128 KB）
 *
 * 启动决策（任务书 P0-03 要求至少两种进入方式，本实现给了两种）：
 *
 *   ① 上电时按下 KEY1（PA0）           -> 进维护模式（串口升级）
 *   ② APP 有效性判断不通过              -> 自动停在维护模式
 *   ③ 都没触发且 APP 有效               -> 直接跳转 APP
 *
 * 界面上只有串口，没有 LCD —— Bootloader 要尽量小、依赖越少越可靠。
 * （如果想让 Bootloader 也点屏，那就要把 LTDC/SDRAM 代码也编进来，
 *   Bootloader 会膨胀到几十 KB，反而更难保证可靠。）
 *
 * 串口：USART1，PA9=TX / PA10=RX，115200 8N1
 *
 * ⚠️ 字符串全部 ASCII：ARMCC 按 GBK 读源文件，UTF-8 中文会吞掉引号。
 ******************************************************************************
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f4xx_hal.h"

#include "app_layout.h"
#include "crc32.h"
#include "ymodem.h"
#include "boot_jump.h"
#include "flash_if.h"

/* ============================================================================
 * 句柄与全局
 * ========================================================================== */
UART_HandleTypeDef huart1;

static YmCtx    g_ym;
static uint32_t g_write_offset = 0;
static uint8_t  g_erase_done   = 0;

/* ---------------- "APP 请求维护" 的约定 ----------------
 * APP 把魔数写进 RTC 备份寄存器（BKP0R）然后软复位，
 * Bootloader 开机读它 —— 这样"请求"能穿过一次复位。
 *
 * 为什么不用普通 RAM：复位后 RAM 内容不保证（尤其断电复位）。
 * 为什么不用 Flash 标志位：写 Flash 要擦整扇区，为一个字节不值得。
 * 备份寄存器由 VBAT/主电源供电保持，正好干这个。 */
#define BOOT_REQ_MAGIC   0xB0071A70u
#define RTC_BKP0_REG     (*(volatile uint32_t *)0x40002850u)

/* 读：有没有挂起的维护请求（读完立刻清掉，避免每次复位都进维护）*/
static int boot_req_pending(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_RTC_ENABLE();              /* 访问 BKP 寄存器需要 RTC APB 时钟 */
    HAL_PWR_EnableBkUpAccess();

    if (RTC_BKP0_REG == BOOT_REQ_MAGIC)
    {
        RTC_BKP0_REG = 0u;               /* 消费掉，下次不再触发 */
        return 1;
    }
    return 0;
}

/* ============================================================================
 * 串口输出（不用 printf 重定向，避免半主机的坑）
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
 * 时钟与引脚（与 APP 保持一致，避免跳转后频率不一样）
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
    clk.APB1CLKDivider = RCC_HCLK_DIV4;      /* 45 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;      /* 90 MHz */

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) { for (;;) { } }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* KEY1 = PA0，下拉，按下为高 */
    g.Pin  = GPIO_PIN_0;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);

    /* 板上 RGB LED 共阳极，低电平亮：先用红灯表示"在 Bootloader" */
    g.Pin   = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &g);

    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_10, GPIO_PIN_RESET);   /* 红灯亮 */
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
 * YMODEM 写入回调：第一包数据到达时才擦除
 *
 * 为什么不在进入维护模式就擦：
 *   如果用户只是误按了按键又离开，一擦就把正在跑的 APP 删了。
 *   等到真的收到固件数据再擦，误触的代价降为零。
 * ========================================================================== */
static int ym_write(void *user, const uint8_t *data, uint32_t len)
{
    (void)user;

    if (!g_erase_done)
    {
        FlashIfResult r;

        uart_puts("  erasing APP area (sectors 5..11, 896 KB)...");
        r = FlashIf_EraseApp();

        if (r != FLASHIF_OK)
        {
            uart_printf(" FAIL (%s)\r\n", FlashIf_ResultName(r));
            return -1;
        }
        uart_puts(" ok\r\n");
        g_erase_done = 1;
    }

    if (FlashIf_Write(g_write_offset, data, len) != FLASHIF_OK)
    {
        uart_printf("  write failed at offset 0x%lX\r\n", (unsigned long)g_write_offset);
        return -1;
    }

    g_write_offset += len;
    return 0;
}

/* ============================================================================
 * 维护模式：YMODEM 接收
 * ========================================================================== */
static void enter_maintenance(void)
{
    uint8_t  b;
    uint32_t crc;
    uint32_t idle_ticks = 0;      /* 超时计数：10 x 100ms = 1s 发一个 'C' */

    uart_puts("\r\n=== BOOTLOADER / maintenance mode ===\r\n");
    uart_printf("APP area : 0x%08lX .. 0x%08lX (%lu KB)\r\n",
                (unsigned long)APP_ADDR, (unsigned long)(APP_END_ADDR - 1u),
                (unsigned long)(APP_SIZE / 1024u));
    uart_puts("Send a .bin over YMODEM (115200, use Xshell / Tera Term / SecureCRT)\r\n");
    uart_puts("Waiting for file...\r\n");

    g_write_offset = 0;
    g_erase_done   = 0;
    Ym_Init(&g_ym, ym_write, NULL, APP_SIZE);

    /* 主循环：收一个字节喂一次状态机；超时时按需发 'C' 邀请开始 */
    for (;;)
    {
        if (HAL_UART_Receive(&huart1, &b, 1u, 100u) == HAL_OK)
        {
            YmReply r = Ym_Feed(&g_ym, b);

            idle_ticks = 0;         /* 有数据进来就重新计时 */

            switch (r)
            {
            case YM_REPLY_ASK_C:
                b = 'C'; HAL_UART_Transmit(&huart1, &b, 1, 100);
                break;
            case YM_REPLY_ACK:
                b = 0x06; HAL_UART_Transmit(&huart1, &b, 1, 100);
                break;
            case YM_REPLY_NAK:
                b = 0x15; HAL_UART_Transmit(&huart1, &b, 1, 100);
                break;
            case YM_REPLY_DONE:
            {
                uart_printf("\r\n  transfer done: %lu bytes written\r\n",
                            (unsigned long)Ym_BytesWritten(&g_ym));

                if (Ym_BytesWritten(&g_ym) == 0u)
                {
                    uart_puts("  nothing written, back to waiting\r\n");
                    Ym_Init(&g_ym, ym_write, NULL, APP_SIZE);
                    g_write_offset = 0;
                    g_erase_done   = 0;
                    break;
                }

                /* 读回校验：对整个有效区间算 CRC32 */
                crc = Crc32_Calc(FlashIf_Ptr(0), Ym_BytesWritten(&g_ym));

                if (FlashIf_VerifyCrc(0, Ym_BytesWritten(&g_ym), crc) != FLASHIF_OK)
                {
                    uart_puts("  !!! read-back verify FAILED, image is invalid\r\n");
                    break;
                }

                uart_printf("  read-back CRC32 = 0x%08lX  (verified ok)\r\n",
                            (unsigned long)crc);

                /* 校验向量 */
                {
                    uint32_t msp = 0, entry = 0;
                    AppImgResult vr;

                    FlashIf_ReadVector(&msp, &entry);
                    vr = App_CheckVector(msp, entry);

                    uart_printf("  vector: MSP=0x%08lX entry=0x%08lX -> %s\r\n",
                                (unsigned long)msp, (unsigned long)entry,
                                App_ImgResultName(vr));

                    if (vr == APP_IMG_OK)
                    {
                        uart_puts("  UPGRADE OK. reset the board to run the new APP.\r\n");
                        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_10, GPIO_PIN_SET);
                        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_11, GPIO_PIN_RESET);  /* 绿灯 */
                    }
                    else
                    {
                        uart_puts("  image vector invalid, refusing to jump\r\n");
                    }
                }
                break;
            }
            case YM_REPLY_ABORT:
            {
                uart_puts("\r\n  !!! transfer aborted\r\n");
                /* 让对端看到，然后重置状态重新等 */
                b = 0x18; HAL_UART_Transmit(&huart1, &b, 1, 100);
                b = 0x18; HAL_UART_Transmit(&huart1, &b, 1, 100);
                Ym_Init(&g_ym, ym_write, NULL, APP_SIZE);
                g_write_offset = 0;
                g_erase_done   = 0;
                uart_puts("  back to waiting\r\n");
                break;
            }
            default:
                break;
            }
        }
        else
        {
            /* 超时（每 100ms 一次）。
             * YMODEM 标准是接收方**每秒发一个 'C'** 邀请对端开始；
             * 原来每 100ms 发一个，串口会被 'C' 刷屏
             * （实测刷了 11 万字节，对端工具直接卡住）。 */
            if (!g_ym.got_header)
            {
                idle_ticks++;
                if (idle_ticks >= 10u)
                {
                    idle_ticks = 0;
                    b = 'C';
                    HAL_UART_Transmit(&huart1, &b, 1, 100);
                }
            }
        }

    }
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    uint32_t msp = 0, entry = 0;
    AppImgResult vr;
    uint32_t i;

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    uart_puts("\r\n\r\n");
    uart_puts("==============================================\r\n");
    uart_puts(" STM32F429 Bootloader   v1.0\r\n");
    uart_printf(" build %s %s\r\n", __DATE__, __TIME__);
    uart_puts("==============================================\r\n");

    /* ---------- 读 APP 向量并判断有效性 ---------- */
    FlashIf_ReadVector(&msp, &entry);
    vr = App_CheckVector(msp, entry);

    uart_printf("APP vector: MSP=0x%08lX entry=0x%08lX\r\n",
                (unsigned long)msp, (unsigned long)entry);
    uart_printf("APP status: %s\r\n", App_ImgResultName(vr));

    /* ---------- ② APP 软件请求维护 ----------
     * APP 跑着的时候按 KEY2 或在串口发 'B'，它会写 BKP0R 再软复位。
     * 这一步必须在"跳转 APP"之前判断，否则复位后又会直接跳回 APP，
     * 表现为"按了 B 只重启了一下"。 */
    if (boot_req_pending())
    {
        uart_puts("-> APP requested maintenance (backup register flag)\r\n");
        enter_maintenance();
    }

    /* ---------- ③ 无有效 APP -> 维护模式 ---------- */
    if (vr != APP_IMG_OK)
    {
        uart_puts("-> no valid APP, staying in bootloader\r\n");
        enter_maintenance();
    }

    /* ---------- ④ 2 秒窗口内按 KEY1 -> 维护模式 ---------- */
    uart_puts("Press KEY1 (PA0) within 2 s to enter maintenance...\r\n");

    for (i = 0; i < 20u; i++)
    {
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET)
        {
            uart_puts("-> KEY1 pressed, entering maintenance\r\n");
            enter_maintenance();
        }
        HAL_Delay(100);
    }

    /* ---------- ③ 跳转 APP ---------- */
    uart_printf("-> jumping to APP at 0x%08lX\r\n\r\n", (unsigned long)APP_ADDR);

    HAL_Delay(50);

    BootJump_ToApp(APP_ADDR);

    for (;;) { }
}

/* ============================================================================
 * HAL 回调与断言
 * ========================================================================== */
void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    for (;;) { }
}
#endif
