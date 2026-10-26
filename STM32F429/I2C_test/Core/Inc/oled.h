/**
 ******************************************************************************
 * @file    oled.h
 * @brief   0.96" I2C OLED（SSD1306，128x64）驱动接口
 *
 * 字库说明：
 *   本驱动复用野火资料包里的字库 bsp_oled_codetab.h（F6x8 / F8X16 / F16x16）。
 *   那是**取模数据**，不是逻辑，直接用没问题；
 *   注意它里面的数组是"非 static 定义"，只能被一个 .c 包含 —— 就是本文件对应的 oled.c。
 ******************************************************************************
 */
#ifndef __OLED_H
#define __OLED_H

#include "main.h"

#define OLED_ADDR      0x78   /* SSD1306：7 位地址 0x3C << 1（HAL 要 8 位形式） */
#define OLED_WR_CMD    0x00   /* 控制字节：后面跟的是命令 */
#define OLED_WR_DATA   0x40   /* 控制字节：后面跟的是显存数据 */

#define OLED_WIDTH     128
#define OLED_PAGES     8      /* 64 行 / 8 = 8 页 */

/* 字符尺寸 */
#define OLED_SIZE_6X8     1   /* 6x8：一个字符占 1 页 */
#define OLED_SIZE_8X16    2   /* 8x16：一个字符占 2 页 */

void OLED_Init(void);
void OLED_CLS(void);
void OLED_Fill(uint8_t data);
void OLED_SetPos(uint8_t x, uint8_t y);

void OLED_ShowChar(uint8_t x, uint8_t y, char ch, uint8_t size);
void OLED_ShowStr(uint8_t x, uint8_t y, const char *str, uint8_t size);

/* 十进制无符号整数显示 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t size);

/* 拓展任务：显示字库 F16x16 里的第 index 个汉字（16x16） */
void OLED_ShowCN(uint8_t x, uint8_t y, uint8_t index);

#endif /* __OLED_H */
