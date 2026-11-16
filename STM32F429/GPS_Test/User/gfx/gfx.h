/**
 ******************************************************************************
 * @file    gfx.h
 * @brief   图形原语（直接写 RGB565 显存，不用 DMA2D）
 *
 * 为什么不用野火的 LCD_DrawLine / LCD_DrawFullRect：
 *   它们每次调用都 DMA2D_DeInit() + Init() + 等 TC，一帧里画几十次会和
 *   LTDC 抢 SDRAM，实测表现为"一动就花屏"。
 *   而且野火的 LCD_DrawLine 垂直分支把字节偏移当像素算，
 *   所有"垂直线"都会变成斜线横扫屏幕。
 *
 * 所以这里自己算、自己写显存。CPU 写显存只影响对应那几个像素。
 *
 * 颜色统一用 24 位 0xRRGGBB，内部转 RGB565。
 ******************************************************************************
 */
#ifndef __GFX_H
#define __GFX_H

#include <stdint.h>

/* 屏幕尺寸：来自 bsp_lcd.h / pc_bsp_lcd.h 的 LCD_PIXEL_WIDTH/HEIGHT */

/** 24 位色 0xRRGGBB -> RGB565 */
uint16_t Gfx_To565(uint32_t rgb888);

/** 填矩形（带裁剪，写到屏幕外会自动截断） */
void Gfx_Fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rgb888);

/** 圆角矩形 */
void Gfx_FillRound(int16_t x, int16_t y, int16_t w, int16_t h,
                   int16_t r, uint32_t rgb888);

/** 圆角矩形 + 2px 描边 */
void Gfx_FillRoundBorder(int16_t x, int16_t y, int16_t w, int16_t h,
                         int16_t r, uint32_t fill, uint32_t edge);

/** 实心圆 */
void Gfx_FillCircle(int16_t cx, int16_t cy, int16_t r, uint32_t rgb888);

/** 直线（Bresenham，2px 粗） */
void Gfx_Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t rgb888);

/** 画字符串
 *  ⚠️ 内部处理了野火 LCD_DisplayChar 的两个坑：
 *     ① 参数是"像素"不是"字符格"
 *     ② 顺序是【纵, 横】不是【横, 纵】 */
void Gfx_Text(int16_t x, int16_t y, const char *s, uint32_t fg, uint32_t bg);

/** 字符串占的像素宽度（当前字体 16x24，所以是 字符数 × 16） */
int16_t Gfx_TextWidth(const char *s);

/** 整屏填充（比逐行 Gfx_Fill 稍快，少一次函数调用） */
void Gfx_Clear(uint32_t rgb888);

#endif /* __GFX_H */
