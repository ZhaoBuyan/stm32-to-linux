/**
 ******************************************************************************
 * @file    main.c
 * @brief   姿态感知交互游戏 —— 用 MPU6050 倾斜控制小球
 *
 * 平台：野火 STM32F429 挑战者 V2 + 7寸 800x480 RGB 电容屏
 *
 * 分层：
 *     MPU6050  ──倾角──▶  game.c（物理/碰撞/关卡）
 *                              ▲
 *     触摸/按键 ──点击──▶  app.c（状态机/按钮）
 *                              ▼
 *                         render.c（LCD 绘制，局部刷新）
 *
 * 基座是野火的 LCD 例程：SDRAM/LTDC/触摸/串口都用它的。
 * 这样就不必从零配 LTDC 那 30 多根引脚和 SDRAM 时序。
 ******************************************************************************
 */
#include "stm32f4xx.h"
#include "./led/bsp_led.h"
#include "./usart/bsp_debug_usart.h"
#include "./sdram/bsp_sdram.h"
#include "./lcd/bsp_lcd.h"
#include "./touch/gt9xx.h"
#include "./systick/bsp_SysTick.h"

#include "./game/game.h"
#include "./game/app.h"
#include "./game/render.h"
#include "./game/bsp_tick.h"
#include "./imu/mpu6050_bb.h"

/* ---------------- 全局对象 ---------------- */
static App_Ctx g_app;
static MPU_Dev g_imu;

/* 静止时的加速度参考（水平基准） */
static int16_t g_ax_ref = 0;
static int16_t g_ay_ref = 0;

/* 采样加速度求平均，作为"放平"的基准 */
static void CalibrateLevel(uint16_t samples)
{
    int32_t sx = 0, sy = 0;
    uint16_t i, ok = 0;
    MPU_Raw d;

    if (samples == 0) { samples = 1; }

    for (i = 0; i < samples; i++)
    {
        if (MPU_ReadRaw(&g_imu, &d) == MPU_OK)
        {
            sx += d.ax;
            sy += d.ay;
            ok++;
        }
        Delay_ms(5);
    }

    if (ok)
    {
        g_ax_ref = (int16_t)(sx / (int32_t)ok);
        g_ay_ref = (int16_t)(sy / (int32_t)ok);
    }
}

int main(void)
{
    uint8_t  id = 0;
    uint32_t last_ctrl = 0, last_touch = 0;
    uint8_t  was_touching = 0;

    /* ---------------- 底层初始化（顺序照野火例程） ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    printf("\r\n=== F429 tilt ball game ===\r\n");

    GTP_Init_Panel();           /* 触摸初始化：顺带把 PB6/PB7 配成软件 I2C */
    SysTick_Init();             /* Delay_ms 用 */
    Tick_Init();                /* TIM6 -> 1ms 节拍，游戏用 */

    LCD_Init();
    LCD_LayerInit();
    LTDC_Cmd(ENABLE);

    LCD_SetLayer(LCD_BACKGROUND_LAYER);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetLayer(LCD_FOREGROUND_LAYER);
    LCD_SetTransparency(0xFF);
    LCD_Clear(LCD_COLOR_BLACK);

    LCD_SetFont(&LCD_DEFAULT_FONT);

    printf("LCD ready\r\n");

    /* ---------------- MPU6050 ---------------- */
    if (MPU_ReadWhoAmI(&id) != MPU_OK)
    {
        printf("IMU WHO_AM_I failed (got 0x%02X)\r\n", (unsigned)id);
        LED_RED;
        while (1) { }
    }
    printf("IMU WHO_AM_I = 0x%02X  OK\r\n", (unsigned)id);

    if (MPU_Init(&g_imu) != MPU_OK)
    {
        printf("IMU init failed\r\n");
        LED_RED;
        while (1) { }
    }

    printf("calibrating: keep the board FLAT and STILL ...\r\n");
    MPU_CalibrateGyro(&g_imu, 100);
    CalibrateLevel(100);
    printf("gyro bias = (%d, %d, %d)\r\n",
           (int)g_imu.gyro_bias[0], (int)g_imu.gyro_bias[1], (int)g_imu.gyro_bias[2]);
    printf("level ref = ax:%d ay:%d\r\n", (int)g_ax_ref, (int)g_ay_ref);
    printf("ready.\r\n\r\n");

    /* ---------------- 游戏 ---------------- */
    App_Init(&g_app);
    Render_Begin(&g_app);

    LED_BLUE;

    /* ---------------- 主循环 ---------------- */
    while (1)
    {
        uint32_t now = Tick_Get();

        /* ===== 每 20ms：读姿态 + 推进游戏 + 局部刷新 ===== */
        if ((uint32_t)(now - last_ctrl) >= 20)
        {
            uint32_t dtms = (uint32_t)(now - last_ctrl);
            MPU_Raw  d;

            last_ctrl = now;

            if (MPU_ReadRaw(&g_imu, &d) == MPU_OK)
            {
                float tx = (float)((int16_t)(d.ax - g_ax_ref));
                float ty = (float)((int16_t)(d.ay - g_ay_ref));

                App_Update(&g_app, tx, ty, (float)dtms / 1000.0f);
            }

            Render_Frame(&g_app);

            /* 灯跟着状态走 */
            switch (g_app.game.state)
            {
            case GAME_RUNNING: LED_BLUE;   break;
            case GAME_WIN:     LED_GREEN;  break;
            case GAME_FAIL:    LED_RED;    break;
            case GAME_PAUSED:  LED_YELLOW; break;
            default:           LED_WHITE;  break;
            }
        }

        /* ===== 每 50ms：读触摸 ===== */
        if ((uint32_t)(now - last_touch) >= 50)
        {
            int x = 0, y = 0;
            int touching;

            last_touch = now;

            touching = GTP_Execu(&x, &y);

            /* 只在"从没按到按下"那一刻算一次点击，避免按住连发 */
            if (touching && !was_touching)
            {
                if (App_HandleTap(&g_app, (int16_t)x, (int16_t)y))
                {
                    printf("tap (%d,%d) -> %s\r\n", x, y, App_StateName(&g_app));
                    Render_Begin(&g_app);       /* 状态变了，静态层重画 */
                }
            }
            was_touching = (uint8_t)(touching ? 1 : 0);
        }
    }
}
/*********************************************END OF FILE**********************/
