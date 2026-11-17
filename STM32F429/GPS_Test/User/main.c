/**
 ******************************************************************************
 * @file    main.c
 * @brief   GPS 自检 + 解析 + 可读报告
 *
 * 串口（USART1 @115200）不再刷原始 NMEA，而是每 2 秒打一份自检报告：
 *   串口 / 数据流 / 天线 / 卫星数 / 定位状态 / 经纬度 / 速度 / 海拔 / 时间
 * 状态一变化（比如从"未定位"变成"已定位"）会立刻多打一份，不用等 2 秒。
 *
 * 想看原始 NMEA 就把 RAW_ECHO 改成 1。
 *
 * ⚠️ 这里的字符串字面量**全部用 ASCII**：
 *   ARMCC 按 GBK 读源文件，UTF-8 的中文会跨越字节边界，
 *   某些汉字的后半个字节会吞掉后面的引号，导致 "missing closing quote"。
 *   中文只放在注释里 —— 注释不参与语法分析，安全。
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
#include "./gps/fmt.h"

/* ============================================================================
 * 配置
 * ========================================================================== */

/* 1 = 同时把原始 NMEA 打到串口（调试用，会很刷屏）*/
#define RAW_ECHO            0

static const uint32_t k_baud_list[] = { 9600, 115200, 4800, 38400, 57600, 19200 };
#define BAUD_COUNT          (sizeof(k_baud_list) / sizeof(k_baud_list[0]))

#define BAUD_PROBE_MS       1400    /* 每个波特率监听多久 */
#define FIX_STALE_MS        3000    /* 有效定位保鲜期 */
#define LINK_TIMEOUT_MS     5000    /* 多久没语句算链路断 */
#define REPORT_HEARTBEAT_MS 10000   /* 内容没变化时的心跳周期 */

/* ============================================================================
 * 全局状态
 * ========================================================================== */
static GpsFix   g_fix;
static char     s_last_line[GPS_LINE_MAX];
static uint8_t  s_locked = 0;
static uint32_t s_locked_baud = 0;
static uint8_t  s_printed_first_fix = 0;   /* 首次定位时把原始 RMC 打一次，方便留样本 */

/* 报告的"指纹"：只有这些字段变化才值得重新打印。
 * 语句数、定位年龄这种一直在变的字段故意不算进去 ——
 * 否则每 2 秒都会重打一份，串口全是重复内容，反而看不清哪个变了。 */
typedef struct
{
    int      state;
    int      had_fix;
    uint8_t  sats_visible;
    uint8_t  sats_used;
    uint8_t  quality;
    int32_t  lat_e7;
    int32_t  lon_e7;
    uint32_t speed_kmh_x1000;
    uint32_t bad_cs;
    uint32_t dropped;
    uint8_t  antenna_warned;
} ReportKey;

static void make_report_key(const GpsFix *g, ReportKey *k)
{
    memset(k, 0, sizeof(*k));
    k->state            = (int)g->state;
    k->had_fix          = g->had_fix;
    k->sats_visible     = g->sats_visible;
    k->sats_used        = g->sats_used;
    k->quality          = g->quality;
    k->lat_e7           = g->fix.lat_e7;
    k->lon_e7           = g->fix.lon_e7;
    k->speed_kmh_x1000  = g->fix.speed_kmh_x1000;
    k->bad_cs           = g->bad_checksum;
    k->dropped          = GpsUart_DroppedBytes();
    k->antenna_warned   = (g->antenna_warn_count > 0) ? 1u : 0u;
}

/* ============================================================================
 * 自检报告（串口）
 * ========================================================================== */
static void print_report(uint32_t now)
{
    char lat_s[24], lon_s[24];
    const char *mark;

    printf("\r\n=============== GPS SELF-TEST ===============\r\n");

    /* 1. uart */
    if (s_locked)
    {
        printf("[01] uart      USART3 @%-6lu              PASS\r\n",
               (unsigned long)s_locked_baud);
    }
    else
    {
        printf("[01] uart      USART3 not locked          FAIL  <-- check wiring/baud\r\n");
    }

    /* 2. data flow
     * 加了两个东西：
     *   - 坏校验和的**比例**（光看绝对数没感觉，3 条到底是 0.07% 还是 30%？）
     *   - 环形缓冲**丢了多少字节**（大于 0 说明主循环被卡住了，需要查）*/
    {
        uint32_t rate = 0;      /* 百分比 × 100，例如 7 表示 0.07% */
        uint32_t dropped = GpsUart_DroppedBytes();

        if (g_fix.lines > 0)
        {
            uint32_t b = (g_fix.bad_checksum > 100000u) ? 100000u : g_fix.bad_checksum;
            rate = (b * 10000u) / g_fix.lines;
        }

        if (g_fix.lines > 0 && g_fix.state != GPS_NO_DATA)
        {
            printf("[02] data      %lu lines / %lu bad-cs (%lu.%02lu%%)  PASS\r\n",
                   (unsigned long)g_fix.lines, (unsigned long)g_fix.bad_checksum,
                   (unsigned long)(rate / 100u), (unsigned long)(rate % 100u));
        }
        else
        {
            printf("[02] data      %lu lines / %lu bad-cs (%lu.%02lu%%)  FAIL  <-- no data\r\n",
                   (unsigned long)g_fix.lines, (unsigned long)g_fix.bad_checksum,
                   (unsigned long)(rate / 100u), (unsigned long)(rate % 100u));
        }

        if (dropped == 0)
        {
            printf("               rx buffer: no bytes dropped      PASS\r\n");
        }
        else
        {
            printf("               rx buffer: DROPPED %lu bytes      WARN  <-- main loop too slow\r\n",
                   (unsigned long)dropped);
        }
    }

    /* 3. module text —— **只陈述事实，不做判断**
     *
     * 这一行以前叫 "[03] antenna"，还给了 PASS/FAIL。那是错的：
     * 我们并没有测天线，只是在 NMEA 文本里找 "ANTENNA OPEN" 这几个字。
     * 模块不报 ≠ 天线好（换一个不发 GPTXT 的模块，这里会永远显示 PASS）。
     * 所以现在：
     *   - 去掉 PASS/FAIL，只报告"模块说了什么"
     *   - 真正的天线结论交给 [04] sats：有星就是能用，没星才是问题
     */
    if (g_fix.antenna_warn_count > 0)
    {
        printf("[03] module    ANTENNA OPEN reported       (module text, not measured)\r\n");
    }
    else if (g_fix.lines >= GPS_ANTENNA_CONFIRM_LINES)
    {
        printf("[03] module    no warnings                (module text, not measured)\r\n");
    }
    else
    {
        printf("[03] module    --\r\n");
    }

    /* 4. satellites
     * 判定必须看**数量**，不能看"有没有收到过 GSV"：
     * 模块一直在发 GSV，只是里面写着 00 颗，has_gsv 为真但一颗星也没有。
     * （第一版就是按 has_gsv 判的，结果这一行没有 PASS/FAIL，看不出结论。）
     *
     * 这一行才是天线的**真正结论**：
     *   visible > 0         -> 天线在收信号，能用
     *   visible = 0 且没过窗口 -> 室内遮挡，先换位置再判断
     *   visible = 0 且在室外   -> 天线确实不通
     */
    if (g_fix.sats_visible > 0 || g_fix.sats_used > 0)
    {
        printf("[04] sats      visible %-3u used %-3u      %s\r\n",
               (unsigned)g_fix.sats_visible, (unsigned)g_fix.sats_used,
               (g_fix.sats_used > 0) ? "PASS" : "WAIT");
    }
    else
    {
        printf("[04] sats      visible 0   used 0          FAIL  <-- go outdoors / near window\r\n");
        if (g_fix.antenna_warn_count > 0)
        {
            printf("               (module also reports ANTENNA OPEN -- if this happens\r\n");
            printf("                outdoors too, the antenna is really not connected)\r\n");
        }
    }

    /* 5. fix state */
    if (g_fix.state == GPS_FIX_VALID)      { mark = "PASS"; }
    else if (g_fix.state == GPS_NO_DATA)   { mark = "FAIL"; }
    else                                   { mark = "WAIT"; }

    printf("[05] fix       %-11s %-9s      %s\r\n",
           GpsFix_StateName(g_fix.state), GpsFix_QualityName(g_fix.quality), mark);

    /* 6..12 only when a fix has ever been obtained */
    if (g_fix.had_fix)
    {
        Fmt_E7(lat_s, (int)sizeof(lat_s), g_fix.fix.lat_e7);
        Fmt_E7(lon_s, (int)sizeof(lon_s), g_fix.fix.lon_e7);

        printf("[06] lat       %s %c\r\n", lat_s, (g_fix.fix.lat_e7 < 0) ? 'S' : 'N');
        printf("[07] lon       %s %c\r\n", lon_s, (g_fix.fix.lon_e7 < 0) ? 'W' : 'E');

        /* 再加两种更常用的写法：
         *   dms  —— 度分秒，人读友好
         *   copy —— "纬度,经度"，直接粘到地图 App 就能定位 */
        {
            char dms_lat[32], dms_lon[32];

            Fmt_Dms(dms_lat, (int)sizeof(dms_lat), g_fix.fix.lat_e7, 'N', 'S');
            Fmt_Dms(dms_lon, (int)sizeof(dms_lon), g_fix.fix.lon_e7, 'E', 'W');

            printf("     dms       %s  %s\r\n", dms_lat, dms_lon);
            printf("     copy      %s,%s      <-- paste into a map app\r\n",
                   lat_s, lon_s);
        }

        printf("[08] speed     %lu.%03lu km/h\r\n",
               (unsigned long)(g_fix.fix.speed_kmh_x1000 / 1000u),
               (unsigned long)(g_fix.fix.speed_kmh_x1000 % 1000u));

        if (g_fix.has_alt)
        {
            printf("[09] altitude  %ld m\r\n", (long)g_fix.alt_m);
        }

        if (g_fix.has_hdop)
        {
            printf("[10] hdop      %u.%u\r\n",
                   (unsigned)(g_fix.hdop_x10 / 10u),
                   (unsigned)(g_fix.hdop_x10 % 10u));
        }

        if (g_fix.fix.has_time)
        {
            uint32_t ms = g_fix.fix.utc_ms_of_day;
            printf("[11] utc       %02lu:%02lu:%02lu\r\n",
                   (unsigned long)(ms / 3600000u),
                   (unsigned long)((ms / 60000u) % 60u),
                   (unsigned long)((ms / 1000u) % 60u));
        }

        {
            uint32_t age = GpsFix_AgeMs(&g_fix, now);
            if (age == 0xFFFFFFFFu)
            {
                printf("[12] fix age   --\r\n");
            }
            else
            {
                printf("[12] fix age   %lu ms%s\r\n",
                       (unsigned long)age,
                       (age > FIX_STALE_MS) ? "   (STALE)" : "");
            }
        }
    }
    else
    {
        printf("[06] position  none yet (never had a fix)\r\n");
    }

    printf("=============================================\r\n");
}

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
    case GPS_STALE:       return "had a fix, now stale";
    default:              return "no data - check wiring / power / baud";
    }
}

static void lcd_status(uint32_t now)
{
    char buf[96];
    char lat_s[24], lon_s[24];

    Gfx_Clear(COL_BG);

    Gfx_Text(16, 12, "GPS  selftest + fix", COL_TITLE, COL_BG);
    sprintf(buf, "USART3 %lu baud %s", (unsigned long)s_locked_baud,
            s_locked ? "LOCKED" : "SEARCH");
    Gfx_Text(470, 12, buf, s_locked ? COL_DIM : COL_WARN, COL_BG);

    /* 定位子状态 */
    Gfx_FillRoundBorder(16, 50, 768, 56, 10, 0x1A2230, 0x2A3442);
    Gfx_Text(32, 68, GpsFix_StateName(g_fix.state), state_color(g_fix.state), 0x1A2230);
    Gfx_Text(230, 68, state_hint(g_fix.state), COL_DIM, 0x1A2230);

    /* 自检三行 */
    /* 模块自述（不是我们测的），所以不显示 OK/坏，只显示它说了什么 */
    sprintf(buf, "module: %s", (g_fix.antenna_warn_count > 0) ? "ANTENNA OPEN" : "no warning");
    Gfx_Text(16, 124, buf,
             (g_fix.antenna_warn_count > 0) ? COL_ERR : COL_DIM, COL_BG);

    sprintf(buf, "sats: %u visible / %u used",
            (unsigned)g_fix.sats_visible, (unsigned)g_fix.sats_used);
    Gfx_Text(300, 124, buf, (g_fix.sats_visible > 0) ? COL_OK : COL_WARN, COL_BG);

    sprintf(buf, "lines %lu  bad-cs %lu",
            (unsigned long)g_fix.lines, (unsigned long)g_fix.bad_checksum);
    Gfx_Text(600, 124, buf, COL_DIM, COL_BG);

    /* 坐标 / 速度 */
    if (g_fix.had_fix)
    {
        Fmt_E7(lat_s, (int)sizeof(lat_s), g_fix.fix.lat_e7);
        Fmt_E7(lon_s, (int)sizeof(lon_s), g_fix.fix.lon_e7);

        Gfx_Text(16, 160, "lat", COL_DIM, COL_BG);
        Gfx_Text(70, 160, lat_s, COL_TITLE, COL_BG);

        Gfx_Text(340, 160, "lon", COL_DIM, COL_BG);
        Gfx_Text(394, 160, lon_s, COL_TITLE, COL_BG);

        sprintf(buf, "speed %lu.%03lu km/h",
                (unsigned long)(g_fix.fix.speed_kmh_x1000 / 1000u),
                (unsigned long)(g_fix.fix.speed_kmh_x1000 % 1000u));
        Gfx_Text(16, 190, buf, COL_TITLE, COL_BG);

        if (g_fix.has_alt)
        {
            sprintf(buf, "alt %ld m", (long)g_fix.alt_m);
            Gfx_Text(340, 190, buf, COL_DIM, COL_BG);
        }

        {
            uint32_t age = GpsFix_AgeMs(&g_fix, now);
            if (age != 0xFFFFFFFFu)
            {
                sprintf(buf, "age %lu ms", (unsigned long)age);
                Gfx_Text(560, 190, buf, (age > FIX_STALE_MS) ? COL_ERR : COL_OK, COL_BG);
            }
        }
    }
    else
    {
        Gfx_Text(16, 160, "no position yet", COL_DIM, COL_BG);
    }

    /* 最后一条原始语句 */
    Gfx_Text(16, 230, "last line:", COL_DIM, COL_BG);
    Gfx_FillRoundBorder(16, 254, 768, 40, 8, 0x1A2230, 0x2A3442);
    if (s_last_line[0] != '\0')
    {
        char shown[49];
        strncpy(shown, s_last_line, 48);
        shown[48] = '\0';
        Gfx_Text(28, 262, shown, COL_ACCENT, 0x1A2230);
    }
    else
    {
        Gfx_Text(28, 262, "(waiting)", COL_DIM, 0x1A2230);
    }

    Gfx_Text(16, 312, "wiring: GPS TX -> PB11 (USART3_RX)", COL_DIM, COL_BG);
    Gfx_Text(16, 338, "        GPS RX -> PB10 (optional)", COL_DIM, COL_BG);
    Gfx_Text(16, 364, "        VCC/GND per module label", COL_DIM, COL_BG);

    sprintf(buf, "uptime %lu s", (unsigned long)(now / 1000u));
    Gfx_Text(620, 440, buf, COL_DIM, COL_BG);
}

/* ============================================================================
 * 自动波特率搜索
 * ========================================================================== */
static uint32_t probe_baud(uint32_t baud)
{
    Gps_LineAsm la;
    uint32_t    start, got = 0, good = 0;

    GpsUart_Init(baud);
    GpsUart_Reset();
    Gps_LineReset(&la);

    printf("  try %lu baud ... ", (unsigned long)baud);

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
                good++;
                break;
            }
        }
    }

    if (good) { printf("%lu bytes, PASS\r\n", (unsigned long)got); }
    else      { printf("%lu bytes, no valid line\r\n", (unsigned long)got); }

    return good;
}

static uint32_t find_baud(void)
{
    unsigned i;

    for (i = 0; i < BAUD_COUNT; i++)
    {
        if (probe_baud(k_baud_list[i]) > 0) { return k_baud_list[i]; }
    }
    return 0;
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    Gps_LineAsm la;
    uint32_t    last_draw = 0;
    uint32_t    last_tick = 0;
    uint32_t    last_report = 0xFFFFFF00u;   /* 保证第一轮先打一份 */
    ReportKey   last_key;          /* 上一次报告时的关键字段快照 */

    /* ---------------- 1. base ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    SysTick_Init();
    Tick_Init();

    printf("\r\n==========================================\r\n");
    printf(" GPS selftest   USART3 (PB10=TX / PB11=RX)\r\n");
    printf("==========================================\r\n");

    /* ---------------- 2. LCD ---------------- */
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

    GpsFix_Init(&g_fix, FIX_STALE_MS, LINK_TIMEOUT_MS);
    Gps_LineReset(&la);

    /* ---------------- 3. find baud ---------------- */
    printf("\r\n[1/3] scanning baud rates\r\n");
    s_locked_baud = find_baud();

    if (s_locked_baud != 0)
    {
        s_locked = 1;
        printf("      locked at %lu baud\r\n", (unsigned long)s_locked_baud);
        LED_GREEN;
    }
    else
    {
        s_locked = 0;
        s_locked_baud = 9600;
        printf("      NOT locked, listening at 9600 anyway\r\n");
        printf("      check: GPS VCC/GND, GPS TX -> PB11, module LED\r\n");
        LED_RED;
        GpsUart_Init(9600);
    }

    GpsUart_Reset();
    Gps_LineReset(&la);

    printf("\r\n[2/3] receiving\r\n");
    printf("[3/3] full report when something changes, heartbeat every %d s\r\n",
           REPORT_HEARTBEAT_MS / 1000);
    make_report_key(&g_fix, &last_key);
    lcd_status(Tick_Get());

    /* ---------------- 4. main loop ---------------- */
    while (1)
    {
        uint32_t now = Tick_Get();
        int      c;

        while ((c = GpsUart_ReadByte()) >= 0)
        {
            int r = Gps_LineFeed(&la, c);

            if (r == 1)
            {
                const char *text = Gps_LineText(&la);

                GpsFix_OnLine(&g_fix, text, now);

                /* 首次定位成功时，把那条 RMC 原样打一次：
                 * 这是抓"真实定位数据"最方便的办法 —— 不用开 RAW_ECHO 刷屏，
                 * 也不会错过唯一的那一条。 */
                if (!s_printed_first_fix && g_fix.state == GPS_FIX_VALID)
                {
                    s_printed_first_fix = 1;
                    char fl[24], fo[24], dl[32], dn[32];

                    Fmt_E7(fl, (int)sizeof(fl), g_fix.fix.lat_e7);
                    Fmt_E7(fo, (int)sizeof(fo), g_fix.fix.lon_e7);
                    Fmt_Dms(dl, (int)sizeof(dl), g_fix.fix.lat_e7, 'N', 'S');
                    Fmt_Dms(dn, (int)sizeof(dn), g_fix.fix.lon_e7, 'E', 'W');

                    printf("\r\n>>> FIRST FIX\r\n");
                    printf(">>> copy  %s,%s        <-- paste into a map app\r\n", fl, fo);
                    printf(">>> dms   %s  %s\r\n", dl, dn);
                    printf(">>> nmea  %s\r\n\r\n", text);
                }

                strncpy(s_last_line, text, sizeof(s_last_line) - 1);
                s_last_line[sizeof(s_last_line) - 1] = '\0';

#if RAW_ECHO
                printf("%s\r\n", text);
#endif

                switch (g_fix.state)
                {
                case GPS_FIX_VALID:   LED_GREEN;  break;
                case GPS_STALE:       LED_YELLOW; break;
                default:              LED_BLUE;   break;
                }
            }
            else if (r == -1)
            {
                printf("[dropped an overlong sentence]\r\n");
            }
        }

        if ((uint32_t)(now - last_tick) >= 100)
        {
            last_tick = now;
            GpsFix_Tick(&g_fix, now);
        }

        /* 报告打印策略：
         *   - 关键字段（状态/卫星数/坐标/速度/坏校验和/丢字节）有变化 -> 立刻打
         *   - 都没变   -> 每 REPORT_HEARTBEAT_MS 打一次心跳，证明还活着
         * 这样串口不会被重复内容刷屏，但真正变了的东西一眼就能看到。 */
        {
            ReportKey key;
            make_report_key(&g_fix, &key);

            if (memcmp(&key, &last_key, sizeof(key)) != 0)
            {
                if (key.state != last_key.state)
                {
                    printf("\r\n>>> state changed: %s -> %s\r\n",
                           GpsFix_StateName((GpsState)last_key.state),
                           GpsFix_StateName((GpsState)key.state));
                }
                print_report(now);
                last_key = key;
                last_report = now;
            }
            else if ((uint32_t)(now - last_report) >= REPORT_HEARTBEAT_MS)
            {
                last_report = now;
                printf("[heartbeat] %s  lines %lu  sats %u/%u  age %lu ms\r\n",
                       GpsFix_StateName(g_fix.state),
                       (unsigned long)g_fix.lines,
                       (unsigned)g_fix.sats_visible, (unsigned)g_fix.sats_used,
                       (unsigned long)GpsFix_AgeMs(&g_fix, now));
            }
        }

        if ((uint32_t)(now - last_draw) >= 500)
        {
            last_draw = now;
            lcd_status(now);
        }
    }
}

/*********************************************END OF FILE**********************/
