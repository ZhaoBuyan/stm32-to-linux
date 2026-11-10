/**
 ******************************************************************************
 * @file    main.c
 * @brief   Template_LCD —— 7 寸彩屏工程的"底座模板"
 *
 * 这个工程的目的：**以后每个新项目都从它复制开始**，不用再配一遍
 * LTDC / SDRAM / 7 寸屏时序 —— 那些踩过的坑已经固化在这里了。
 *
 * 已经调通并固化的：
 *     SDRAM    FMC bank2 @ 0xD0000000，16 位，CAS 2
 *     LTDC     800x480 RGB565，PLLSAI 420/7/4 = 15 MHz（约 32.8 Hz）
 *     屏参数   cur_lcd = INCH_7（野火工程默认是 INCH_5，7 寸必须改）
 *     图形库   gfx.c —— 直接写显存，不用 DMA2D（DMA2D 会和 LTDC 抢总线）
 *     时基     TIM6 1 ms
 *     串口     USART1 115200
 *     按键     KEY1=PA0，KEY2=PC13
 *     LED      PH10/11/12 共阳极，低电平亮
 *
 * 本文件只做两件事：
 *     1. 按正确顺序初始化
 *     2. 每 20 ms 调一次 Demo_Draw()
 *
 * 换成你自己的项目时：改 Demo_Draw 那一行，换成你自己的业务即可。
 ******************************************************************************
 */
#include <stdio.h>
#include "stm32f4xx.h"
#include "./led/bsp_led.h"
#include "./usart/bsp_debug_usart.h"
#include "./sdram/bsp_sdram.h"
#include "./lcd/bsp_lcd.h"
#include "./systick/bsp_SysTick.h"

#include "./tick/bsp_tick.h"        /* TIM6 1ms 节拍 */
#include "./gfx/gfx.h"
#include "./demo/demo_scene.h"

/* ---------------------------------------------------------------------------
 * 按键：KEY1 = PA0，KEY2 = PC13（板上已有下拉，按下为高）
 * 注意：PA0/PC13 都不会和 LTDC / FMC / SWD 冲突
 * ------------------------------------------------------------------------- */
static void Key_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOC, ENABLE);

    g.GPIO_Mode  = GPIO_Mode_IN;
    g.GPIO_PuPd  = GPIO_PuPd_DOWN;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_OType = GPIO_OType_PP;

    g.GPIO_Pin = GPIO_Pin_0;   GPIO_Init(GPIOA, &g);   /* KEY1 */
    g.GPIO_Pin = GPIO_Pin_13;  GPIO_Init(GPIOC, &g);   /* KEY2 */
}

static int Key1(void) { return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0)  == Bit_SET; }
static int Key2(void) { return GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) == Bit_SET; }

/* ---------------------------------------------------------------------------
 * 屏幕自检：红 → 绿 → 蓝 → 白 → 黑
 *
 * 上电刷一遍，能一眼看出三件事：
 *   ① SDRAM 是否正常（不正常会是花屏/残影）
 *   ② LTDC 时序是否正常（不正常会是黑屏或条纹）
 *   ③ RGB 三个通道有没有接反（红应该真的是红）
 * ------------------------------------------------------------------------- */
static void Screen_SelfTest(void)
{
    printf("screen self-test: RED\r\n");    Gfx_Clear(0xFF0000); Delay_ms(400);
    printf("screen self-test: GREEN\r\n");  Gfx_Clear(0x00FF00); Delay_ms(400);
    printf("screen self-test: BLUE\r\n");   Gfx_Clear(0x0000FF); Delay_ms(400);
    printf("screen self-test: WHITE\r\n");  Gfx_Clear(0xFFFFFF); Delay_ms(400);
    Gfx_Clear(0x10141C);
    printf("screen self-test: done\r\n");
}

int main(void)
{
    uint32_t last = 0;

    /* ---------------- ① 基础外设 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    printf("\r\n=== Template_LCD (7 inch RGB, 800x480, RGB565) ===\r\n");

    SysTick_Init();             /* Delay_ms 用，不产生游戏节拍 */
    Tick_Init();                /* TIM6 -> 1 ms 节拍（中断里只 ++）*/
    Key_Init();

    LED_BLUE;

    /* ---------------- ② 点亮屏幕 ----------------
     * 顺序很重要：LCD_Init 内部会先初始化 SDRAM 再配 LTDC。
     * 不要自己另外调 SDRAM_Init()，会重复初始化。 */
    LCD_Init();
    LCD_LayerInit();
    LTDC_Cmd(ENABLE);

    LCD_SetLayer(LCD_BACKGROUND_LAYER);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetLayer(LCD_FOREGROUND_LAYER);
    LCD_SetTransparency(0xFF);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetFont(&LCD_DEFAULT_FONT);

    printf("LCD ready, cur_lcd = %d (1 = 7 inch)\r\n", (int)cur_lcd);

    /* ---------------- ③ 屏幕自检 ---------------- */
    Screen_SelfTest();

    printf("keys: KEY1=PA0  KEY2=PC13 (press to see state)\r\n");
    printf("drawing demo...\r\n");
    fflush(stdout);

    LED_GREEN;

    /* ---------------- ④ 主循环 ----------------
     * 每 20 ms 画一帧（50 fps 的绘制节奏，实际屏刷新 32.8 Hz）。
     * 换业务时：把 Demo_Draw 换成你自己的 xxx_Draw。 */
    while (1)
    {
        uint32_t now = Tick_Get();

        if ((uint32_t)(now - last) >= 20)
        {
            last = now;

            Demo_Draw(now, Key1(), Key2());

            /* 按键时把灯改成黄/红，直观确认按键通了 */
            if (Key1())      { LED_YELLOW; }
            else if (Key2()) { LED_RED; }
            else             { LED_GREEN; }
        }
    }
}

/*********************************************END OF FILE**********************/
