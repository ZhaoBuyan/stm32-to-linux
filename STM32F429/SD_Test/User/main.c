/**
 ******************************************************************************
 * @file    main.c
 * @brief   SD 卡检测 —— 插卡、识别、读第 0 块
 *
 * 做完这件事，"检测存储器"就算通了：
 *   ① SDIO 初始化成功
 *   ② 卡回了 CID/CSD（说明是真卡，不是噪声）
 *   ③ 容量从 CSD 算出来了
 *   ④ **能读出第 0 块的真实内容**（这一条最关键 —— 前面三条都可能"看起来成功"，
 *      但只有真的读出数据，才证明命令、时钟、数据通道全对）
 *
 * 这一版**不做文件系统**。FatFs 加在它上面，是下一步。
 *
 * ⚠️ 串口输出全部用 ASCII：ARMCC 按 GBK 读源文件，UTF-8 中文会吞掉引号。
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
#include "./sdcard/sdio_sd.h"

/* ============================================================================
 * 屏幕配色
 * ========================================================================== */
#define COL_BG      0x10141C
#define COL_TITLE   0xE6EDF3
#define COL_DIM     0x8B98A9
#define COL_OK      0x35D07F
#define COL_WARN    0xFFB020
#define COL_ERR     0xFF4D5E
#define COL_ACCENT  0x4A90D9

/* ============================================================================
 * 状态
 * ========================================================================== */
static SdCardInfo g_card;
static SdError    g_err      = SD_ERR_NO_CARD;
static int        g_inited   = 0;
static int        g_read_ok  = 0;
static int        g_sig_type = 0;      /* 0=无签名 1=MBR 2=FAT引导扇区 */
static uint8_t    g_sector0[512];
static char       g_detail[64];

/* 判断第 0 块是什么 */
static void check_signature(void)
{
    g_sig_type = 0;

    /* MBR / GPT：偏移 510 处是 0x55 0xAA */
    if (g_sector0[510] == 0x55 && g_sector0[511] == 0xAA)
    {
        g_sig_type = 1;
        strcpy(g_detail, "MBR partition table at 510");
        return;
    }

    /* 无分区表的 FAT（superfloppy）：首字节 0xEB 或 0xE9，
     * 并且偏移 54（FAT12/16）或 82（FAT32）处有 "FAT" */
    if ((g_sector0[0] == 0xEB || g_sector0[0] == 0xE9) &&
        (memcmp(&g_sector0[54], "FAT", 3) == 0 ||
         memcmp(&g_sector0[82], "FAT", 3) == 0))
    {
        g_sig_type = 2;
        strcpy(g_detail, "FAT boot sector (no MBR)");
        return;
    }

    strcpy(g_detail, "no known signature");
}

/* ============================================================================
 * 串口报告
 * ========================================================================== */
static void print_report(void)
{
    uint32_t mb  = g_card.capacity_mb;
    uint32_t gb_x10 = (mb * 10u) / 1024u;
    unsigned i;

    printf("\r\n============== SD CARD TEST ==============\r\n");

    /* 1. init */
    if (g_inited)
    {
        printf("[1] init      SDIO ok, card identified   PASS\r\n");
    }
    else
    {
        printf("[1] init      FAIL: %s\r\n", Sd_ErrorName(g_err));
        printf("              check: card inserted? TF socket J28\r\n");
        printf("                     contacts clean? FAT32 formatted?\r\n");
        printf("==========================================\r\n");
        return;
    }

    /* 2. card type + capacity */
    printf("[2] type      %-6s (CSD v%u, %s addressing)\r\n",
           g_card.is_sdhc ? "SDHC" : "SDSC",
           (unsigned)g_card.csd_version,
           g_card.is_sdhc ? "block" : "byte");
    printf("[3] capacity  %lu MB  (%lu.%lu GB)\r\n",
           (unsigned long)mb,
           (unsigned long)(gb_x10 / 10u), (unsigned long)(gb_x10 % 10u));
    printf("[4] blocks    %lu x 512B\r\n", (unsigned long)g_card.block_count);

    /* 5. identity */
    printf("[5] cid       MID 0x%02X  OID 0x%04X  PNM \"%s\"  PRV %u.%u\r\n",
           (unsigned)g_card.mid, (unsigned)g_card.oid, g_card.pnm,
           (unsigned)((g_card.prv >> 4) & 0x0F), (unsigned)(g_card.prv & 0x0F));
    printf("[6] serial    0x%08lX   RCA 0x%04X\r\n",
           (unsigned long)g_card.psn, (unsigned)g_card.rca);

    /* 7. block 0 */
    if (g_read_ok)
    {
        printf("[7] read blk0 OK  (%s)   PASS\r\n", g_detail);
        printf("              first 16 bytes:");
        for (i = 0; i < 16u; i++) { printf(" %02X", g_sector0[i]); }
        printf("\r\n");
        printf("              bytes 510/511: %02X %02X\r\n",
               g_sector0[510], g_sector0[511]);
    }
    else
    {
        printf("[7] read blk0 FAIL: %s\r\n", Sd_ErrorName(g_err));
    }

    /* 8. verdict */
    if (g_read_ok && g_sig_type != 0)
    {
        printf("[8] verdict   CARD USABLE, ready for FatFs\r\n");
    }
    else if (g_read_ok)
    {
        printf("[8] verdict   card readable but NOT FORMATTED (no MBR/FAT)\r\n");
        printf("              format it as FAT32 on a PC first\r\n");
    }
    else
    {
        printf("[8] verdict   cannot read data\r\n");
    }

    printf("==========================================\r\n");
}

/* ============================================================================
 * 屏幕
 * ========================================================================== */
static void lcd_status(void)
{
    char buf[96];
    uint32_t mb = g_card.capacity_mb;
    uint32_t gb_x10 = (mb * 10u) / 1024u;

    Gfx_Clear(COL_BG);

    Gfx_Text(16, 12, "SD card test   SDIO 1-bit", COL_TITLE, COL_BG);
    Gfx_Text(600, 12, "PC12/PD2/PC8", COL_DIM, COL_BG);

    /* 第 1 行：识别结果 */
    Gfx_FillRoundBorder(16, 50, 768, 56, 10, 0x1A2230, 0x2A3442);
    if (g_inited)
    {
        Gfx_Text(32, 68, "CARD DETECTED", COL_OK, 0x1A2230);
    }
    else
    {
        Gfx_Text(32, 68, "NO CARD", COL_ERR, 0x1A2230);
        Gfx_Text(220, 68, Sd_ErrorName(g_err), COL_DIM, 0x1A2230);
    }

    if (g_inited)
    {
        sprintf(buf, "type      %s  (CSD v%u)",
                g_card.is_sdhc ? "SDHC/SDXC" : "SDSC",
                (unsigned)g_card.csd_version);
        Gfx_Text(16, 124, buf, COL_TITLE, COL_BG);

        sprintf(buf, "capacity  %lu MB  (%lu.%lu GB)",
                (unsigned long)mb,
                (unsigned long)(gb_x10 / 10u), (unsigned long)(gb_x10 % 10u));
        Gfx_Text(16, 154, buf, COL_TITLE, COL_BG);

        sprintf(buf, "blocks    %lu x 512B", (unsigned long)g_card.block_count);
        Gfx_Text(16, 184, buf, COL_DIM, COL_BG);

        sprintf(buf, "MID 0x%02X   OID 0x%04X   PNM \"%s\"",
                (unsigned)g_card.mid, (unsigned)g_card.oid, g_card.pnm);
        Gfx_Text(16, 214, buf, COL_DIM, COL_BG);

        sprintf(buf, "serial 0x%08lX   RCA 0x%04X",
                (unsigned long)g_card.psn, (unsigned)g_card.rca);
        Gfx_Text(16, 244, buf, COL_DIM, COL_BG);

        /* 读块结果 */
        if (g_read_ok)
        {
            sprintf(buf, "read block 0   OK   %s", g_detail);
            Gfx_Text(16, 292, buf, COL_OK, COL_BG);

            sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X   ...   %02X %02X",
                    g_sector0[0], g_sector0[1], g_sector0[2], g_sector0[3],
                    g_sector0[4], g_sector0[5], g_sector0[6], g_sector0[7],
                    g_sector0[510], g_sector0[511]);
            Gfx_Text(16, 322, buf, COL_ACCENT, COL_BG);
        }
        else
        {
            sprintf(buf, "read block 0   FAIL   %s", Sd_ErrorName(g_err));
            Gfx_Text(16, 292, buf, COL_ERR, COL_BG);
        }

        /* 结论 */
        if (g_read_ok && g_sig_type != 0)
        {
            Gfx_Text(16, 372, "CARD USABLE - next step: add FatFs", COL_OK, COL_BG);
        }
        else if (g_read_ok)
        {
            Gfx_Text(16, 372, "NOT FORMATTED - format as FAT32 on PC", COL_WARN, COL_BG);
        }
        else
        {
            Gfx_Text(16, 372, "cannot read data from card", COL_ERR, COL_BG);
        }
    }
    else
    {
        Gfx_Text(16, 124, "check: card inserted in TF socket J28?", COL_DIM, COL_BG);
        Gfx_Text(16, 154, "       contacts clean?", COL_DIM, COL_BG);
        Gfx_Text(16, 184, "       formatted as FAT32?", COL_DIM, COL_BG);
        Gfx_Text(16, 230, "wiring: CLK=PC12  CMD=PD2  D0=PC8", COL_DIM, COL_BG);
        Gfx_Text(16, 260, "        (onboard J28 TF socket)", COL_DIM, COL_BG);
    }

    Gfx_Text(16, 440, "press RESET to retry", COL_DIM, COL_BG);
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    uint32_t tries;

    /* ---------------- 基础外设 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    SysTick_Init();
    Tick_Init();

    printf("\r\n==========================================\r\n");
    printf(" SD card test   SDIO 1-bit (PC12/PD2/PC8)\r\n");
    printf("==========================================\r\n");

    /* ---------------- 屏幕 ---------------- */
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

    /* ---------------- 识别卡 ----------------
     * 重试几次：上电瞬间卡可能还没上电完成，
     * 或者用户刚插卡（卡座弹片接触抖动）。 */
    for (tries = 0; tries < 3u; tries++)
    {
        g_err = Sd_Init(&g_card);

        if (g_err == SD_OK)
        {
            g_inited = 1;
            break;
        }

        printf("attempt %lu failed: %s\r\n",
               (unsigned long)(tries + 1u), Sd_ErrorName(g_err));
        Delay_ms(200);
    }

    /* ---------------- 读第 0 块 ---------------- */
    if (g_inited)
    {
        memset(g_sector0, 0, sizeof(g_sector0));

        g_err = Sd_ReadBlocks(0, g_sector0, 1u);

        if (g_err == SD_OK)
        {
            g_read_ok = 1;
            check_signature();
            LED_GREEN;
        }
        else
        {
            LED_RED;
        }
    }
    else
    {
        LED_RED;
    }

    lcd_status();
    print_report();

    /* ---------------- 主循环：只刷屏，保持状态可见 ---------------- */
    while (1)
    {
        Delay_ms(500);

        /* 如果一开始没认到卡，持续重试（用户可能后插卡）*/
        if (!g_inited)
        {
            g_err = Sd_Init(&g_card);

            if (g_err == SD_OK)
            {
                g_inited = 1;
                g_err = Sd_ReadBlocks(0, g_sector0, 1u);

                if (g_err == SD_OK)
                {
                    g_read_ok = 1;
                    check_signature();
                    LED_GREEN;
                }
                else
                {
                    LED_RED;
                }

                lcd_status();
                print_report();
            }
            else
            {
                /* 每 5 秒重画一次，避免频繁整车刷新 */
                static uint32_t tick_cnt = 0;
                if (++tick_cnt >= 10u)
                {
                    tick_cnt = 0;
                    lcd_status();
                }
            }
        }
    }
}

/*********************************************END OF FILE**********************/
