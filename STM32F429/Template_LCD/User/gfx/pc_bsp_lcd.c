/**
 * @file    pc_bsp_lcd.c
 * @brief   野火 LCD 接口 -> PCFbView 帧缓冲 的适配实现
 */
#include "pc_bsp_lcd.h"
#include <string.h>
#include <stdio.h>

/* 由 preview_adapter.c 定义 */
extern uint16_t *g_fv_fb;
extern int       g_fv_w;
extern int       g_fv_h;

static const FvHost *s_host = NULL;
static uint16_t s_text_color = 0xFFFF;
static uint16_t s_back_color = 0x0000;

void Pc_SetHost(const FvHost *host) { s_host = host; }

void LCD_SetTextColor(uint16_t color565) { s_text_color = color565; }
void LCD_SetBackColor(uint16_t color565) { s_back_color = color565; }

#define GLYPH_W     16
#define GLYPH_H     24

#define RUN_MAX     64

static char     s_run[RUN_MAX];
static int      s_run_len = 0;
static int      s_run_x = 0;
static int      s_run_y = 0;
static uint16_t s_run_color = 0xFFFF;

static void run_flush(void)
{
    if (s_run_len > 0)
    {
        s_run[s_run_len] = '\0';
        if (s_host != NULL && s_host->draw_text != NULL)
        {
            s_host->draw_text(s_run, s_run_x, s_run_y, s_run_color);
        }
        s_run_len = 0;
    }
}

void LCD_DisplayChar(uint16_t y_pixel, uint16_t x_pixel, uint8_t ch)
{
    int16_t x0 = (int16_t)x_pixel;
    int16_t y0 = (int16_t)y_pixel;
    int16_t i, j;

    if (g_fv_fb != NULL && x0 >= 0 && y0 >= 0 &&
        x0 + GLYPH_W <= g_fv_w && y0 + GLYPH_H <= g_fv_h)
    {
        for (j = 0; j < GLYPH_H; j++)
        {
            uint16_t *row = &g_fv_fb[(size_t)(y0 + j) * g_fv_w + (size_t)x0];
            for (i = 0; i < GLYPH_W; i++) { row[i] = s_back_color; }
        }
    }

    if (s_run_len > 0 &&
        y0 == s_run_y &&
        s_text_color == s_run_color &&
        x0 == s_run_x + s_run_len * GLYPH_W &&
        s_run_len < RUN_MAX - 1 &&
        ch >= 32 && ch < 127)
    {
        s_run[s_run_len++] = (char)ch;
        return;
    }

    run_flush();

    if (ch >= 32 && ch < 127)
    {
        s_run_x     = x0;
        s_run_y     = y0;
        s_run_color = s_text_color;
        s_run[0]    = (char)ch;
        s_run_len   = 1;
    }
}

void Pc_FlushText(void) { run_flush(); }