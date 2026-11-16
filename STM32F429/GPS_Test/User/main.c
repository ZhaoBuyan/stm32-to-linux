/**
 ******************************************************************************
 * @file    main.c
 * @brief   GPS 最小验证工程 —— 把 NMEA 原始数据原样打印出来
 *
 * 这一步的目的**不是解析**，而是确认三件事：
 *   ① 接线对不对（GPS TX -> MCU PB11）
 *   ② 波特率对不对
 *   ③ 数据是不是真的（不是假的、不是"无定位"的空白帧）
 *
 * 任务书 M2.1 的原话：
 *   「先用 USB-TTL 或 MCU 串口原样打印/记录 GPS 原始 NMEA；
 *     确认数据源真实、波特率正确，再做结构化解析。」
 *
 * 所以本工程只做「收 + 打印」：
 *   串口1（调试）  -> 把收到的每一行原样打出来
 *   7 寸屏         -> 显示波特率、字节数、最后一行
 *
 * 波特率未知时，上电会自动试一圈（9600 / 115200 / 4800 / 38400 / 57600 / 19200）。
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "stm32f4xx.h"
#include "./led/bsp_led.h"
#include "./usart/bsp_debug_usart.h"
#include "./lcd/bsp_lcd.h"
#include "./systick/bsp_SysTick.h"
#include "./tick/bsp_tick.h"
#include "./gfx/gfx.h"
#include "./gps/gps_uart.h"
#include "./gps/gps_line.h"

/* ============================================================================
 * 配置
 * ========================================================================== */

/* 自动搜索的波特率顺序。知道型号就直接把想用的放第一个，能省 2 秒。 */
static const uint32_t k_baud_list[] = { 9600, 115200, 4800, 38400, 57600, 19200 };
#define BAUD_COUNT  (sizeof(k_baud_list) / sizeof(k_baud_list[0]))

/* 每个波特率监听多久（毫秒）。太短可能正好错过一帧（GPS 常 1 Hz）。 */
#define BAUD_PROBE_MS       1400

/* 一行 NMEA 最长长度（与 gps_line.h 的 GPS_LINE_MAX 一致）*/
#define NMEA_LINE_MAX       GPS_LINE_MAX

/* ============================================================================
 * 屏幕显示
 * ========================================================================== */
#define COL_BG      0x10141C
#define COL_TITLE   0xE6EDF3
#define COL_DIM     0x8B98A9
#define COL_OK      0x35D07F
#define COL_WARN    0xFFB020
#define COL_ERR     0xFF4D5E
#define COL_ACCENT  0x4A90D9

static char  s_last_line[NMEA_LINE_MAX];   /* 最后一条完整语句 */
static char  s_status[64];                 /* 当前状态文字 */
static uint32_t s_line_count = 0;
static uint8_t  s_locked = 0;

static void lcd_status(uint32_t now)
{
    char buf[80];

    Gfx_Clear(COL_BG);

    Gfx_Text(16, 14, "GPS NMEA raw echo", COL_TITLE, COL_BG);
    Gfx_Text(560, 14, "USART3  PB10/PB11", COL_DIM, COL_BG);

    /* 波特率 / 锁定状态 */
    if (s_locked)
    {
        sprintf(buf, "BAUD %lu", (unsigned long)Gps_Baud());
        Gfx_Text(16, 60, buf, COL_OK, COL_BG);
        Gfx_Text(240, 60, "LOCKED", COL_OK, COL_BG);
    }
    else
    {
        sprintf(buf, "BAUD %lu", (unsigned long)Gps_Baud());
        Gfx_Text(16, 60, buf, COL_WARN, COL_BG);
        Gfx_Text(240, 60, "SEARCHING...", COL_WARN, COL_BG);
    }

    /* 计数 */
    sprintf(buf, "bytes  %-10lu  lines  %lu",
            (unsigned long)Gps_TotalBytes(), (unsigned long)s_line_count);
    Gfx_Text(16, 100, buf, COL_DIM, COL_BG);

    /* 最后一行原始数据（超过 48 字符就截断，屏幕一行放得下 50 个字符）*/
    Gfx_Text(16, 150, "last line:", COL_DIM, COL_BG);
    Gfx_FillRoundBorder(16, 178, 768, 44, 8, 0x1A2230, 0x2A3442);
    if (s_last_line[0] != '\0')
    {
        char shown[49];
        strncpy(shown, s_last_line, 48);
        shown[48] = '\0';
        Gfx_Text(28, 188, shown, COL_ACCENT, 0x1A2230);
    }
    else
    {
        Gfx_Text(28, 188, "(no complete line yet)", COL_DIM, 0x1A2230);
    }

    /* 提示 */
    Gfx_Text(16, 250, s_status, (s_locked ? COL_DIM : COL_ERR), COL_BG);

    Gfx_Text(16, 300, "wiring:  GPS TX -> PB11 (USART3_RX)", COL_DIM, COL_BG);
    Gfx_Text(16, 326, "         GPS RX -> PB10 (USART3_TX)  optional", COL_DIM, COL_BG);
    Gfx_Text(16, 352, "         VCC/GND per module label (most: 3.3-5V)", COL_DIM, COL_BG);

    sprintf(buf, "uptime %lu s", (unsigned long)(now / 1000));
    Gfx_Text(16, 440, buf, COL_DIM, COL_BG);
}

/* ============================================================================
 * 自动波特率搜索
 * ========================================================================== */

/* 监听一小段时间，判断这个波特率对不对。
 *
 * 判定条件不是"收到一个 '$'" —— 噪声里也常出现 0x24(也就是 '$')，
 * 那样会误锁到一个错误的波特率上。这里要求：
 *   组出一条**完整的行**，且该行以 '$' 开头、长度至少 6 字节（"$GNRMC" 这种量级）。
 * 组行用的是 gps_line.c，和主循环完全同一份逻辑。
 *
 * @return 收到并识别成功的完整行数（0 表示这个波特率不对）
 */
static uint32_t probe_baud(uint32_t baud)
{
    Gps_LineAsm la;
    uint32_t    start, got = 0, good = 0;

    Gps_Init(baud);
    Gps_Reset();
    Gps_LineReset(&la);

    printf("\r\n--- probing %lu baud ---\r\n", (unsigned long)baud);

    start = Tick_Get();
    while ((uint32_t)(Tick_Get() - start) < BAUD_PROBE_MS)
    {
        int c = Gps_ReadByte();

        if (c < 0) { continue; }

        got++;

        if (Gps_LineFeed(&la, c) == 1)
        {
            const char *text = Gps_LineText(&la);

            if (text[0] == '$' && strlen(text) >= 6)
            {
                printf("    first line: %s\r\n", text);
                good++;
                break;              /* 拿到一条像样的语句就可以锁定了 */
            }
        }
    }

    printf("    %lu byte(s), %lu valid line(s)%s\r\n",
           (unsigned long)got, (unsigned long)good,
           good ? " -> LOCKED" : "");

    return good;
}

static uint32_t find_baud(void)
{
    unsigned i;

    for (i = 0; i < BAUD_COUNT; i++)
    {
        if (probe_baud(k_baud_list[i]) > 0)
        {
            return k_baud_list[i];
        }
    }
    return 0;
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    Gps_LineAsm la;                 /* 组行器：逻辑在 gps_line.c，PC 上已单测过 */
    uint32_t last_draw = 0;
    uint32_t last_rx = 0;
    uint32_t locked_baud = 0;

    /* ---------------- ① 基础外设 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    SysTick_Init();
    Tick_Init();

    printf("\r\n=====================================\r\n");
    printf(" GPS NMEA raw echo  (USART3 PB10/PB11)\r\n");
    printf("=====================================\r\n");

    /* ---------------- ② 屏幕 ---------------- */
    LCD_Init();
    LCD_LayerInit();
    LTDC_Cmd(ENABLE);
    LCD_SetLayer(LCD_BACKGROUND_LAYER);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetLayer(LCD_FOREGROUND_LAYER);
    LCD_SetTransparency(0xFF);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetFont(&LCD_DEFAULT_FONT);

    LED_BLUE;
    strcpy(s_status, "searching baud rate, please wait ...");
    lcd_status(0);

    /* ---------------- ③ 自动找波特率 ---------------- */
    locked_baud = find_baud();

    if (locked_baud != 0)
    {
        s_locked = 1;
        strcpy(s_status, "locked. sending raw lines to USART1 (115200)");
        printf("\r\n*** LOCKED at %lu baud ***\r\n", (unsigned long)locked_baud);
        LED_GREEN;
    }
    else
    {
        s_locked = 0;
        strcpy(s_status, "NO DATA. check wiring / power / baud");
        printf("\r\n*** NO DATA on any tested baud ***\r\n");
        printf("check: GPS VCC+GND, GPS TX -> PB11, module LED blinking?\r\n");
        LED_RED;

        /* 停在 9600 继续听，可能用户接上后就能收到 */
        Gps_Init(9600);
        Gps_Reset();
    }

    s_status[0] = '\0';
    if (s_locked) { strcpy(s_status, "OK. raw lines echoed on USART1 @115200"); }
    else          { strcpy(s_status, "NO DATA - check wiring / power / baud"); }

    Gps_LineReset(&la);
    last_rx = Tick_Get();
    last_draw = 0;

    /* ---------------- ④ 主循环：收 -> 组行 -> 打印 ---------------- */
    while (1)
    {
        int c = Gps_ReadByte();

        if (c >= 0)
        {
            int r = Gps_LineFeed(&la, c);

            last_rx = Tick_Get();

            if (r == 1)
            {
                const char *text = Gps_LineText(&la);

                s_line_count++;

                /* 原样打印出来 —— 这就是本工程的全部目的 */
                printf("%s\r\n", text);

                /* 记到最后一行，用于屏幕显示 */
                strncpy(s_last_line, text, sizeof(s_last_line) - 1);
                s_last_line[sizeof(s_last_line) - 1] = '\0';

                /* 灯闪一下，表示收到数据 */
                if ((s_line_count & 1u) != 0u) { LED_GREEN; }
                else                           { LED_BLUE;  }
            }
            else if (r == -1)
            {
                /* 超长：组行器会一直丢到下一个行尾，避免尾巴污染下一行 */
                printf("\r\n[line too long, discarded until next EOL]\r\n");
            }
        }

        /* 屏幕每 200 ms 刷一次，别每帧重画 */
        if ((uint32_t)(Tick_Get() - last_draw) >= 200)
        {
            last_draw = Tick_Get();
            lcd_status(Tick_Get());
        }

        /* 3 秒没数据就提示一次（只提示，不重置）*/
        if ((uint32_t)(Tick_Get() - last_rx) >= 3000)
        {
            last_rx = Tick_Get();
            printf("[no data for 3s] bytes=%lu lines=%lu\r\n",
                   (unsigned long)Gps_TotalBytes(), (unsigned long)s_line_count);
            LED_RED;
        }
    }
}

/*********************************************END OF FILE**********************/
