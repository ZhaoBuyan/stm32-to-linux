/**
 ******************************************************************************
 * @file    gfx.c
 * @brief   图形原语实现（直接写 RGB565 显存）
 *
 * 平台相关只有一个地方：显存从哪来。
 *   真板子  -> 野火 bsp_lcd.h，显存在 0xD0000000
 *   PC 预览 -> pc_bsp_lcd.h，显存是普通数组
 * 用 PC_PREVIEW 宏切换，其余代码完全一样。
 ******************************************************************************
 */
#include "gfx.h"

#ifdef PC_PREVIEW
#include "pc_bsp_lcd.h"
#else
#include "../lcd/bsp_lcd.h"
#endif

#include <string.h>

/* 当前图层的显存基址
 * 野火是双层显存：前景层 = LCD_FRAME_BUFFER + BUFFER_OFFSET
 * PC 预览里 BUFFER_OFFSET 是 0 */
#define FB_BASE     ((volatile uint8_t *)(LCD_FRAME_BUFFER + BUFFER_OFFSET))

/* 当前字体宽度：Font16x24 */
#define GFX_FONT_W  16

/* ============================================================================
 * 内部工具
 * ========================================================================== */

static int16_t gfx_isqrt(uint32_t v)
{
    int16_t h = 0;
    while ((uint32_t)(h + 1) * (uint32_t)(h + 1) <= v) { h++; }
    return h;
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */

uint16_t Gfx_To565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 0xF8u) << 8) |
                      (((c >> 8)  & 0xFCu) << 3) |
                      (( c        & 0xF8u) >> 3));
}

void Gfx_Fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rgb888)
{
    uint16_t c565 = Gfx_To565(rgb888);
    uint8_t lo = (uint8_t)(c565 & 0xFF);
    uint8_t hi = (uint8_t)(c565 >> 8);
    int16_t i, j;

    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if ((int32_t)x + w > LCD_PIXEL_WIDTH)  { w = (int16_t)(LCD_PIXEL_WIDTH  - x); }
    if ((int32_t)y + h > LCD_PIXEL_HEIGHT) { h = (int16_t)(LCD_PIXEL_HEIGHT - y); }
    if (w <= 0 || h <= 0) { return; }

    for (j = 0; j < h; j++)
    {
        volatile uint8_t *p = FB_BASE + 2u *
            ((uint32_t)(y + j) * LCD_PIXEL_WIDTH + (uint32_t)x);
        for (i = 0; i < w; i++)
        {
            p[0] = lo;
            p[1] = hi;
            p += 2;
        }
    }
}

void Gfx_Clear(uint32_t rgb888)
{
    Gfx_Fill(0, 0, (int16_t)LCD_PIXEL_WIDTH, (int16_t)LCD_PIXEL_HEIGHT, rgb888);
}

void Gfx_FillRound(int16_t x, int16_t y, int16_t w, int16_t h,
                   int16_t r, uint32_t rgb888)
{
    int16_t j;

    if (r > w / 2) { r = (int16_t)(w / 2); }
    if (r > h / 2) { r = (int16_t)(h / 2); }
    if (r < 0) { r = 0; }

    for (j = 0; j < h; j++)
    {
        int16_t inset = 0;

        if (j < r)
        {
            int16_t d = (int16_t)(r - j);
            inset = (int16_t)(r - gfx_isqrt((uint32_t)(r * r - d * d)));
        }
        else if (j > (int16_t)(h - 1 - r))
        {
            int16_t d = (int16_t)(j - (h - 1 - r));
            inset = (int16_t)(r - gfx_isqrt((uint32_t)(r * r - d * d)));
        }

        Gfx_Fill((int16_t)(x + inset), (int16_t)(y + j),
                 (int16_t)(w - inset * 2), 1, rgb888);
    }
}

void Gfx_FillRoundBorder(int16_t x, int16_t y, int16_t w, int16_t h,
                         int16_t r, uint32_t fill, uint32_t edge)
{
    Gfx_FillRound(x, y, w, h, r, edge);
    Gfx_FillRound((int16_t)(x + 2), (int16_t)(y + 2),
                  (int16_t)(w - 4), (int16_t)(h - 4),
                  (int16_t)(r > 2 ? r - 2 : 0), fill);
}

void Gfx_FillCircle(int16_t cx, int16_t cy, int16_t r, uint32_t rgb888)
{
    int16_t dy;

    for (dy = -r; dy <= r; dy++)
    {
        int32_t w2 = (int32_t)r * r - (int32_t)dy * dy;
        int16_t half = (w2 > 0) ? gfx_isqrt((uint32_t)w2) : 0;

        Gfx_Fill((int16_t)(cx - half), (int16_t)(cy + dy),
                 (int16_t)(half * 2 + 1), 1, rgb888);
    }
}

void Gfx_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t rgb888)
{
    int16_t dx = (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    int16_t dy = (int16_t)((y1 > y0) ? (y0 - y1) : (y1 - y0));   /* 负数 */
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    for (;;)
    {
        Gfx_Fill(x0, y0, 2, 2, rgb888);      /* 2px 粗，1px 在彩屏上看不清 */
        if (x0 == x1 && y0 == y1) { break; }
        {
            int16_t e2 = (int16_t)(2 * err);
            if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
            if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
        }
    }
}

void Gfx_Text(int16_t x, int16_t y, const char *s, uint32_t fg, uint32_t bg)
{
    LCD_SetTextColor(Gfx_To565(fg));
    LCD_SetBackColor(Gfx_To565(bg));

    while (*s)
    {
        LCD_DisplayChar((uint16_t)y, (uint16_t)x, (uint8_t)(*s));
        x = (int16_t)(x + GFX_FONT_W);
        s++;
    }
}

int16_t Gfx_TextWidth(const char *s)
{
    if (s == NULL) { return 0; }
    return (int16_t)((int16_t)strlen(s) * GFX_FONT_W);
}
