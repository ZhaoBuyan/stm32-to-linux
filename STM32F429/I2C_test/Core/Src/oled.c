/**
 ******************************************************************************
 * @file    oled.c
 * @brief   0.96" I2C OLED（SSD1306）驱动实现 —— 硬件 I2C + HAL
 *
 * 提供：写命令/写数据、初始化序列、清屏、定位、字符/字符串、十进制数字、汉字
 *
 * 字库：bsp_oled_codetab.h（野火提供，放在 Core/Inc/ 下，只由本文件包含）
 ******************************************************************************
 */
#include "oled.h"
#include <stdio.h>
#include "bsp_oled_codetab.h"   /* F6x8[][] / F8X16[] / F16x16[] —— 只能在这里包含 */

extern I2C_HandleTypeDef hi2c1;

/* ============================ 底层两个函数 ============================
 * SSD1306 用"控制字节"区分命令和数据：
 *   0x00 → 后面是命令
 *   0x40 → 后面是显存数据
 * 所以一次调用 = "写 1 个字节到 I2C 地址 0x78 的寄存器 0x00/0x40"
 * ==================================================================== */
static void OLED_WriteCmd(uint8_t cmd)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, OLED_WR_CMD,
                      I2C_MEMADD_SIZE_8BIT, &cmd, 1, 100);
}

/* 连续写多个显存字节：**一次** I2C 事务发完。
 * SSD1306 收到 0x40 控制字节后，会把后续字节按列自动递增写进 GDDRAM。
 *
 * 为什么要批量：一次 I2C 事务的 START/地址/控制字节/STOP 开销约 380 µs，
 * 而真正传 1 个字节只要 90 µs —— 逐字节写有 80% 的时间花在"开事务"上。
 * 实测：整屏 1024 字节，逐字节 ≈ 480 ms，按页批量 ≈ 96 ms。 */
static void OLED_WriteDataBuf(const uint8_t *buf, uint16_t len)
{
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, OLED_WR_DATA,
                      I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, 100);
}

/* ============================== 初始化 ==============================
 * 这一串是 SSD1306 的标准初始化序列（128x64、内部电荷泵）。
 * 数据来自野火配套例程，属于"芯片手册里的配置表"，不是自己编的。
 * ================================================================== */
void OLED_Init(void)
{
    OLED_WriteCmd(0xAE);        /* 关显示 */

    OLED_WriteCmd(0x81);        /* 设置对比度 */
    OLED_WriteCmd(0xFF);        /* 0x00~0xFF，越大越亮 */

    OLED_WriteCmd(0xA4);        /* 按 RAM 内容显示（0xA5 = 全亮） */
    OLED_WriteCmd(0xA6);        /* 正常显示（0xA7 = 反色） */

    OLED_WriteCmd(0x2E);        /* 关闭滚动 */

    OLED_WriteCmd(0x20);        /* 寻址模式 */
    OLED_WriteCmd(0x10);        /* 0x10 = 页寻址模式 */
    OLED_WriteCmd(0xB0);        /* 起始页 = 0 */
    OLED_WriteCmd(0x00);        /* 起始列低 4 位 */
    OLED_WriteCmd(0x10);        /* 起始列高 4 位 */

    OLED_WriteCmd(0x40);        /* 显示起始行 = 0 */
    OLED_WriteCmd(0xA1);        /* 列重映射：左右方向 */
    OLED_WriteCmd(0xC8);        /* COM 扫描方向：上下方向 */

    OLED_WriteCmd(0xA8);        /* 多路复用比 */
    OLED_WriteCmd(0x3F);        /* 0x3F + 1 = 64 行 */

    OLED_WriteCmd(0xD3);        /* 显示偏移 */
    OLED_WriteCmd(0x00);
    OLED_WriteCmd(0xDA);        /* COM 引脚配置 */
    OLED_WriteCmd(0x12);
    OLED_WriteCmd(0xD9);        /* 预充电周期 */
    OLED_WriteCmd(0x22);
    OLED_WriteCmd(0xDB);        /* VCOMH 电平 */
    OLED_WriteCmd(0x20);

    OLED_WriteCmd(0x8D);        /* 电荷泵 */
    OLED_WriteCmd(0x14);        /* 0x14 = 开启 */

    OLED_WriteCmd(0xAF);        /* 开显示 */

    OLED_CLS();
}

/**
 * @brief  设置写入起点
 * @param  x 列 0~127
 * @param  y 页 0~7（每页 8 行）
 * @note   低 4 位那行**不要**写成 `(x & 0x0F) | 0x01`（野火原版有这一笔），
 *         那样会把列地址强制成奇数，整个画面右偏 1 像素。
 */
void OLED_SetPos(uint8_t x, uint8_t y)
{
    OLED_WriteCmd((uint8_t)(0xB0 + y));
    OLED_WriteCmd((uint8_t)(((x & 0xF0) >> 4) | 0x10));
    OLED_WriteCmd((uint8_t)(x & 0x0F));
}

/**
 * @brief  全屏填同一个字节
 * @note   先把一整页填进缓冲，再一次发出去（逐字节写会慢 5 倍）
 */
void OLED_Fill(uint8_t data)
{
    uint8_t page, col;
    uint8_t line[OLED_WIDTH];

    for (col = 0; col < OLED_WIDTH; col++)
    {
        line[col] = data;
    }

    for (page = 0; page < OLED_PAGES; page++)
    {
        OLED_SetPos(0, page);
        OLED_WriteDataBuf(line, OLED_WIDTH);
    }
}

/**
 * @brief  清屏
 */
void OLED_CLS(void)
{
    OLED_Fill(0x00);
}

/**
 * @brief  显示一个 ASCII 字符
 * @param  size OLED_SIZE_6X8（占 1 页）或 OLED_SIZE_8X16（占 2 页）
 * @note   整字符一次事务发完：6x8 发 6 字节，8x16 分两页各发 8 字节
 */
void OLED_ShowChar(uint8_t x, uint8_t y, char ch, uint8_t size)
{
    uint8_t c = (uint8_t)ch - 32;   /* 字库从空格(0x20)开始 */

    if (c > 95) c = 0;              /* 超出可打印 ASCII 就画空格，别画花屏 */

    if (size == OLED_SIZE_6X8)
    {
        OLED_SetPos(x, y);
        OLED_WriteDataBuf(F6x8[c], 6);
    }
    else /* OLED_SIZE_8X16：上半页 8 字节 + 下半页 8 字节 */
    {
        OLED_SetPos(x, y);
        OLED_WriteDataBuf(&F8X16[c * 16], 8);
        OLED_SetPos(x, y + 1);
        OLED_WriteDataBuf(&F8X16[c * 16 + 8], 8);
    }
}

/**
 * @brief  显示字符串（自动换行）
 */
void OLED_ShowStr(uint8_t x, uint8_t y, const char *str, uint8_t size)
{
    uint8_t w = (size == OLED_SIZE_6X8) ? 6 : 8;
    uint8_t ph = (size == OLED_SIZE_6X8) ? 1 : 2;   /* 每个字符占几页 */

    while (*str != '\0')
    {
        if ((uint8_t)(x + w) > OLED_WIDTH)      /* 到右边界就换行 */
        {
            x = 0;
            y += ph;
        }
        if ((uint8_t)(y + ph) > OLED_PAGES)     /* 到底边界就停，别越界写 */
        {
            return;
        }

        OLED_ShowChar(x, y, *str, size);
        x = (uint8_t)(x + w);
        str++;
    }
}

/**
 * @brief  显示十进制无符号整数
 *
 * 先把数字转成字符串，再复用字符串显示 —— 不用自己拆位，也不会漏掉 num == 0。
 * tmp 开 12 字节：32 位无符号最大 4294967295（10 位）+ '\0' + 余量。
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t size)
{
    char tmp[12];

    sprintf(tmp, "%lu", (unsigned long)num);
    OLED_ShowStr(x, y, tmp, size);
}

/**
 * @brief  显示字库里的第 index 个汉字（16x16，占 2 页）
 * @note   拓展任务用：想显示自己的名字，先用取模软件把字加进 F16x16，
 *         再按索引调用（第 0 个、第 1 个……）。
 */
void OLED_ShowCN(uint8_t x, uint8_t y, uint8_t index)
{
    uint16_t adder = (uint16_t)(32 * index);   /* 每个 16x16 汉字 = 32 字节 */

    OLED_SetPos(x, y);
    OLED_WriteDataBuf(&F16x16[adder], 16);

    OLED_SetPos(x, y + 1);
    OLED_WriteDataBuf(&F16x16[adder + 16], 16);
}
