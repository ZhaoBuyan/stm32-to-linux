/**
 ******************************************************************************
 * @file    main.c
 * @brief   GPS 验证工程：原始数据回显 + NMEA 解析 + 定位状态显示
 *
 * 分两层，都能单独验证：
 *   ① 回显层   把收到的每一行原样打到 USART1  -> 证明链路通
 *   ② 解析层   nmea / rmc / gps_fix 三个纯 C 模块 -> 已用 PC 单测覆盖 120 条
 *
 * 屏幕上会显示当前定位子状态（NO_DATA / FIX_INVALID / FIX_VALID / STALE），
 * 这样不上串口也能一眼看出 GPS 好不好。
 *
 * 硬件（USART3）：
 *   GPS TX  -> MCU PB11 (USART3_RX)
 *   GPS RX  -> MCU PB10 (USART3_TX)   可选
 *   VCC / GND 按模块标注
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
#include "./gps/nmea.h"
#include "./gps/gps_fix.h"

/* ============================================================================
 * 配置
 * ========================================================================== */
static const uint32_t k_baud_list[] = { 9600, 115200, 4800, 38400, 57600, 19200 };
#define BAUD_COUNT          (sizeof(k_baud_list) / sizeof(k_baud_list[0]))

#define BAUD_PROBE_MS       1400    /* 每个波特率监听多久 */
#define FIX_STALE_MS        3000    /* 有效定位保鲜期：3 秒没有新的有效定位就 STALE */
#define LINK_TIMEOUT_MS     5000    /* 5 秒没有任何语句就认为链路断 */

/* ============================================================================
 * 全局状态
 * ========================================================================== */
static GpsFix   g_fix;
static char     s_last_line[GPS_LINE_MAX];
static uint32_t s_line_count = 0;
static uint8_t  s_locked = 0;

/* ============================================================================
 * 屏幕
 * ========================================================================== */
#define COL_BG      0x10141C
#define COL_TITLE   0xE6EDF3
#define COL_DIM     0x8B98A9
#define COL_OK      0x35D07F
#define COL_WARN    0xFFB020
#define COL_ERR     0xFF4D5E
#define COL_ACCENT  0x4A90D9

static uint32_t state_color(GpsState s)
{
    switch (s)
    {
    case GPS_FIX_VALID:   return COL_OK;
    case GPS_FIX_INVALID: return COL_WARN;
    case GPS_STALE:       return COL_ERR;
    default:              return COL_DIM;
    }
}

static const char *state_hint(GpsState s)
{
    switch (s)
    {
    case GPS_FIX_VALID:   return "position is fresh";
    case GPS_FIX_INVALID: return "no fix yet - go outdoors / near a window";
    case GPS_STALE:       return "had a fix, now stale - check antenna/sky view";
    default:              return "no data - check wiring / power / baud";
    }
}

/* 把 ×1e7 定点度格式化成 "31.8451022" 这样的字符串 */
static void fmt_e7(char *buf, size_t n, int32_t e7)
{
    int32_t a = (e7 < 0) ? -e7 : e7;
    snprintf(buf, n, "%s%ld.%07ld",
             (e7 < 0) ? "-" : "",
             (long)(a / 10000000), (long)(a % 10000000));
}

static void lcd_status(uint32_t now)
{
    char buf[96];
    char lat_s[24], lon_s[24];

    Gfx_Clear(COL_BG);

    /* 标题 */
    Gfx_Text(16, 12, "GPS  NMEA + fix state", COL_TITLE, COL_BG);
    sprintf(buf, "USART3  %lu baud  %s", (unsigned long)GpsUart_Baud(),
            s_locked ? "LOCKED" : "search");
    Gfx_Text(420, 12, buf, s_locked ? COL_DIM : COL_WARN, COL_BG);

    /* 定位子状态（最重要的一行）*/
    Gfx_FillRoundBorder(16, 50, 768, 56, 10, 0x1A2230, 0x2A3442);
    Gfx_Text(32, 68, GpsFix_StateName(g_fix.state), state_color(g_fix.state), 0x1A2230);
    Gfx_Text(230, 68, state_hint(g_fix.state), COL_DIM, 0x1A2230);

    /* 坐标 / 速度 / 定位年龄 */
    if (g_fix.had_fix)
    {
        fmt_e7(lat_s, sizeof(lat_s), g_fix.fix.lat_e7);
        fmt_e7(lon_s, sizeof(lon_s), g_fix.fix.lon_e7);

        Gfx_Text(16, 124, "lat", COL_DIM, COL_BG);
        Gfx_Text(70, 124, lat_s, COL_TITLE, COL_BG);

        Gfx_Text(340, 124, "lon", COL_DIM, COL_BG);
        Gfx_Text(394, 124, lon_s, COL_TITLE, COL_BG);

        sprintf(buf, "speed %lu.%03lu km/h",
                (unsigned long)(g_fix.fix.speed_kmh_x1000 / 1000u),
                (unsigned long)(g_fix.fix.speed_kmh_x1000 % 1000u));
        Gfx_Text(16, 152, buf, COL_TITLE, COL_BG);

        {
            uint32_t age = GpsFix_AgeMs(&g_fix, now);
            if (age == 0xFFFFFFFFu)
            {
                Gfx_Text(340, 152, "age --", COL_DIM, COL_BG);
            }
            else
            {
                sprintf(buf, "age %lu ms", (unsigned long)age);
                Gfx_Text(340, 152, buf,
                         (age > FIX_STALE_MS) ? COL_ERR : COL_OK, COL_BG);
            }
        }

        if (g_fix.fix.has_time)
        {
            uint32_t ms = g_fix.fix.utc_ms_of_day;
            sprintf(buf, "utc %02lu:%02lu:%02lu",
                    (unsigned long)(ms / 3600000u),
                    (unsigned long)((ms / 60000u) % 60u),
                    (unsigned long)((ms / 1000u) % 60u));
            Gfx_Text(560, 152, buf, COL_DIM, COL_BG);
        }
    }
    else
    {
        Gfx_Text(16, 124, "no position yet", COL_DIM, COL_BG);
    }

    /* 计数器 */
    sprintf(buf, "lines %-8lu  bad-cs %-6lu  not-rmc %lu",
            (unsigned long)g_fix.lines,
            (unsigned long)g_fix.bad_checksum,
            (unsigned long)g_fix.not_rmc);
    Gfx_Text(16, 190, buf, COL_DIM, COL_BG);

    /* 最后一条原始语句 */
    Gfx_Text(16, 226, "last line:", COL_DIM, COL_BG);
    Gfx_FillRoundBorder(16, 250, 768, 40, 8, 0x1A2230, 0x2A3442);
    if (s_last_line[0] != '\0')
    {
        char shown[49];
        strncpy(shown, s_last_line, 48);
        shown[48] = '\0';
        Gfx_Text(28, 258, shown, COL_ACCENT, 0x1A2230);
    }
    else
    {
        Gfx_Text(28, 258, "(waiting)", COL_DIM, 0x1A2230);
    }

    /* 提示与接线 */
    Gfx_Text(16, 308, "wiring: GPS TX -> PB11 (USART3_RX)", COL_DIM, COL_BG);
    Gfx_Text(16, 334, "        GPS RX -> PB10 (optional)", COL_DIM, COL_BG);
    Gfx_Text(16, 360, "        VCC/GND per module label", COL_DIM, COL_BG);

    sprintf(buf, "uptime %lu s", (unsigned long)(now / 1000u));
    Gfx_Text(600, 440, buf, COL_DIM, COL_BG);
}

/* ============================================================================
 * 自动波特率搜索
 *
 * 判定条件不是"收到一个 '$'" —— 噪声里也常出现 0x24，那样会误锁到错误波特率。
 * 这里要求：组出一条**完整的行**，且以 '$' 开头、长度 >= 6、**校验和通过**。
 * 组行用 gps_line.c，校验用 nmea.c，和主循环完全同一套逻辑。
 * ========================================================================== */
static uint32_t probe_baud(uint32_t baud)
{
    Gps_LineAsm la;
    uint32_t    start, got = 0, good = 0;

    GpsUart_Init(baud);
    GpsUart_Reset();
    Gps_LineReset(&la);

    printf("\r\n--- probing %lu baud ---\r\n", (unsigned long)baud);

    start = Tick_Get();
    while ((uint32_t)(Tick_Get() - start) < BAUD_PROBE_MS)
    {
        int c = GpsUart_ReadByte();

        if (c < 0) { continue; }

        got++;

        if (Gps_LineFeed(&la, c) == 1)
        {
            const char *text = Gps_LineText(&la);

            if (text[0] == '$' && strlen(text) >= 6 && Nmea_ChecksumOk(text))
            {
                printf("    first good line: %s\r\n", text);
                good++;
                break;
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
    Gps_LineAsm la;
    uint32_t    locked_baud;
    uint32_t    last_draw = 0;
    uint32_t    last_tick = 0;

    /* ---------------- ① 基础外设 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    SysTick_Init();
    Tick_Init();

    printf("\r\n==========================================\r\n");
    printf(" GPS NMEA echo + parser  (USART3 PB10/PB11)\r\n");
    printf("==========================================\r\n");

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

    /* 定位状态机：3 秒保鲜、5 秒断链 */
    GpsFix_Init(&g_fix, FIX_STALE_MS, LINK_TIMEOUT_MS);
    Gps_LineReset(&la);

    /* ---------------- ③ 自动找波特率 ---------------- */
    locked_baud = find_baud();

    if (locked_baud != 0)
    {
        s_locked = 1;
        printf("\r\n*** LOCKED at %lu baud ***\r\n", (unsigned long)locked_baud);
        LED_GREEN;
    }
    else
    {
        s_locked = 0;
        printf("\r\n*** NO DATA on any tested baud ***\r\n");
        printf("check: GPS VCC+GND, GPS TX -> PB11, module LED blinking?\r\n");
        LED_RED;

        /* 停在 9600 继续听，接上线就能收到 */
        GpsUart_Init(9600);
        GpsUart_Reset();
    }

    /* 探测期间可能已经把缓冲填满了，清掉重新开始 */
    GpsUart_Reset();
    Gps_LineReset(&la);
    lcd_status(Tick_Get());

    /* ---------------- ④ 主循环 ---------------- */
    while (1)
    {
        uint32_t now = Tick_Get();
        int      c;

        /* 收字节 -> 组行 -> 回显 -> 喂解析器 */
        while ((c = GpsUart_ReadByte()) >= 0)
        {
            int r = Gps_LineFeed(&la, c);

            if (r == 1)
            {
                const char *text = Gps_LineText(&la);

                s_line_count++;

                /* ① 原样打印：证明链路通、数据真 */
                printf("%s\r\n", text);

                /* ② 喂给定位状态机（内部会做校验和、解析、状态迁移）*/
                GpsFix_OnLine(&g_fix, text, now);

                /* 记下来给屏幕显示 */
                strncpy(s_last_line, text, sizeof(s_last_line) - 1);
                s_last_line[sizeof(s_last_line) - 1] = '\0';

                /* 灯随状态变 */
                switch (g_fix.state)
                {
                case GPS_FIX_VALID:   LED_GREEN;  break;
                case GPS_STALE:       LED_YELLOW; break;
                default:              LED_BLUE;   break;
                }
            }
            else if (r == -1)
            {
                printf("\r\n[line too long, discarded until next EOL]\r\n");
            }
        }

        /* 状态机每 100 ms tick 一次，负责超时判定 */
        if ((uint32_t)(now - last_tick) >= 100)
        {
            last_tick = now;
            GpsFix_Tick(&g_fix, now);
        }

        /* 屏幕每 200 ms 刷一次 */
        if ((uint32_t)(now - last_draw) >= 200)
        {
            last_draw = now;
            lcd_status(now);
        }
    }
}

/*********************************************END OF FILE**********************/
