/**
 * @file    pc_bsp_lcd.h
 * @brief   把「野火 LCD 接口」映射到 PCFbView 的帧缓冲
 *
 * 这个文件属于本项目，不属于 PCFbView。
 * 它让 render.c 一行不改就能在 PC 上工作。
 */
#ifndef __PC_BSP_LCD_H
#define __PC_BSP_LCD_H

#include <stdint.h>
#include "fbview.h"

/* 由 preview_adapter.c 在 Pv_Attach 里填好 */
extern uint16_t *g_fv_fb;
extern int       g_fv_w;
extern int       g_fv_h;

#define LCD_PIXEL_WIDTH     (g_fv_w)
#define LCD_PIXEL_HEIGHT    (g_fv_h)

/* 必须是 uintptr_t（64 位）：Windows 上 gcc 默认编 64 位，
   用 uint32_t 会把指针截断，变成野指针。 */
#define LCD_FRAME_BUFFER    ((uintptr_t)g_fv_fb)
#define BUFFER_OFFSET       ((uintptr_t)0)

#define LCD_COLOR_BLACK     0x000000
#define LCD_COLOR_WHITE     0xFFFFFF
#define LCD_COLOR_RED       0xFF0000
#define LCD_COLOR_GREEN     0x00FF00
#define LCD_COLOR_BLUE      0x0000FF
#define LCD_COLOR_GREY      0x808080

/* 野火的签名写的是 (Line, Column)，但实测第一个参数是纵(y)、
   第二个是横(x)，单位都是像素。必须原样保留。 */
void LCD_SetTextColor(uint16_t color565);
void LCD_SetBackColor(uint16_t color565);
void LCD_DisplayChar(uint16_t y_pixel, uint16_t x_pixel, uint8_t ch);

/* 告诉 pc_bsp_lcd.c 宿主是谁（用它的 draw_text 画字）*/
void Pc_SetHost(const FvHost *host);

/* 把本帧累积的字符交给宿主画出来 */
void Pc_FlushText(void);

#endif /* __PC_BSP_LCD_H */