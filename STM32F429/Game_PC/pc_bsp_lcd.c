/**
 ******************************************************************************
 * @file    pc_bsp_lcd.c
 * @brief   假显存 + 野火 LCD 接口的最小实现
 *
 * render.c 只用到两样东西：
 *   1. 一块可以直接写字节的显存（RGB565）
 *   2. LCD_SetTextColor / LCD_SetBackColor / LCD_DisplayChar
 *
 * 这里把「画字」简化成两件事：
 *   - 先把字符格的背景刷成 back_color（和真机行为一致）
 *   - 再把「这个位置应该显示什么字、什么颜色」记进一个队列
 *   main_pc.c 贴完位图后用 GDI 把文字画上去 —— 这样不用自带字模。
 ******************************************************************************
 */
#include "pc_bsp_lcd.h"
#include <string.h>

/* ---------------- 假显存 ---------------- */
uint16_t g_pc_fb[LCD_PIXEL_WIDTH * LCD_PIXEL_HEIGHT];

/* ---------------- 当前颜色 ---------------- */
static uint16_t s_text_color = 0xFFFF;      /* 默认白 */
static uint16_t s_back_color = 0x0000;      /* 默认黑 */

void LCD_SetTextColor(uint16_t color565) { s_text_color = color565; }
void LCD_SetBackColor(uint16_t color565) { s_back_color = color565; }

/* ---------------- 文字队列 ---------------- */

static PcGlyph s_glyphs[PC_MAX_GLYPHS];
static int     s_glyph_count = 0;

/* 字符格尺寸：render.c 里 FONT_W = 16，字高 24 */
#define GLYPH_W     16
#define GLYPH_H     24

void LCD_DisplayChar(uint16_t y_pixel, uint16_t x_pixel, uint8_t ch)
{
    int16_t x0 = (int16_t)x_pixel;
    int16_t y0 = (int16_t)y_pixel;
    int16_t i, j;

    /* ① 照真机行为：先把字符格的背景刷成 back_color */
    if (x0 >= 0 && y0 >= 0 &&
        x0 + GLYPH_W <= LCD_PIXEL_WIDTH &&
        y0 + GLYPH_H <= LCD_PIXEL_HEIGHT)
    {
        for (j = 0; j < GLYPH_H; j++)
        {
            uint16_t *row = &g_pc_fb[(size_t)(y0 + j) * LCD_PIXEL_WIDTH + (size_t)x0];
            for (i = 0; i < GLYPH_W; i++) { row[i] = s_back_color; }
        }
    }

    /* ② 记下"这个位置要显示这个字符"，交给 main_pc.c 用 GDI 画 */
    if (s_glyph_count < PC_MAX_GLYPHS)
    {
        s_glyphs[s_glyph_count].x        = x0;
        s_glyphs[s_glyph_count].y        = y0;
        s_glyphs[s_glyph_count].ch       = (char)ch;
        s_glyphs[s_glyph_count].color565 = s_text_color;
        s_glyph_count++;
    }
}

int Pc_TakeGlyphs(PcGlyph *out, int max)
{
    int n = s_glyph_count;

    if (n > max) { n = max; }
    if (out != NULL && n > 0)
    {
        memcpy(out, s_glyphs, (size_t)n * sizeof(PcGlyph));
    }
    s_glyph_count = 0;
    return n;
}

int Pc_HasGlyphs(void)
{
    return s_glyph_count;
}
