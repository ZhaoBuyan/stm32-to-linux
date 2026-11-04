/**
 ******************************************************************************
 * @file    pc_bsp_lcd.h
 * @brief   PC 预览器的「假板级 LCD 头文件」
 *
 * 目的：让 Game_LCD/User/game/render.c 以为自己在一块 800x480 RGB565 的屏幕上。
 *
 * 真板子：  render.c -> 写物理显存 0xD0000000（LTDC 硬件扫描）
 * PC 预览： render.c -> 写 g_pc_fb 这块普通数组（再由 GDI 画到窗口）
 ******************************************************************************
 */
#ifndef __PC_BSP_LCD_H
#define __PC_BSP_LCD_H

#include <stdint.h>
#include <stddef.h>

/* ---------- 屏幕参数（必须和固件一致）---------- */
#define LCD_PIXEL_WIDTH     800
#define LCD_PIXEL_HEIGHT    480

/* ---------- 假显存 ---------- */
/* 固件里是物理地址 0xD0000000 + BUFFER_OFFSET；
   这里就是一块普通 RAM 数组（RGB565，每像素 2 字节）*/
extern uint16_t  g_pc_fb[LCD_PIXEL_WIDTH * LCD_PIXEL_HEIGHT];

/* ⚠️ 必须用 uintptr_t（64 位下就是 64 位），不能用 uint32_t：
   Windows 上 gcc 默认编 64 位，uint32_t 会把指针地址截断，
   render.c 里 (uint8_t*)(LCD_FRAME_BUFFER + BUFFER_OFFSET) 就变成野指针。 */
#define LCD_FRAME_BUFFER    ((uintptr_t)g_pc_fb)
#define BUFFER_OFFSET       ((uintptr_t)0)

/* ---------- 颜色常量（24 位 0xRRGGBB，和固件一致）---------- */
#define LCD_COLOR_BLACK     0x000000
#define LCD_COLOR_WHITE     0xFFFFFF
#define LCD_COLOR_RED       0xFF0000
#define LCD_COLOR_GREEN     0x00FF00
#define LCD_COLOR_BLUE      0x0000FF
#define LCD_COLOR_GREY      0x808080

/* ---------- 文本接口 ---------- */
/* ⚠️ 野火的签名写的是 (Line, Column)，但实测第一个参数是【纵-y】、
      第二个是【横-x】，单位都是像素。必须原样保留这个怪签名，
      否则 render.c 里已有的调用会全部错位。 */
void LCD_SetTextColor(uint16_t color565);
void LCD_SetBackColor(uint16_t color565);
void LCD_DisplayChar(uint16_t y_pixel, uint16_t x_pixel, uint8_t ch);

/* ---------- 给 main_pc.c 用的文字收集钩子 ---------- */
/* 一帧最多收集多少个字符（render.c 每帧最多画几百个）*/
#define PC_MAX_GLYPHS   1024
typedef struct {
    int16_t  x, y;
    char     ch;
    uint16_t color565;
} PcGlyph;

/* 取走本帧累积的文字（调用后内部清空），返回个数 */
int  Pc_TakeGlyphs(PcGlyph *out, int max);

/* 本帧有没有画过字（用来判断要不要重画窗口）*/
int  Pc_HasGlyphs(void);

#endif /* __PC_BSP_LCD_H */
