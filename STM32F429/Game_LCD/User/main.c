/**
 ******************************************************************************
 * @file    main.c
 * @brief   姿态感知交互游戏 —— MPU6050 倾斜控制小球
 *
 * 平台：野火 STM32F429 挑战者 V2 + 7寸 800x480 RGB 电容屏
 *
 * 初始化顺序很重要：
 *   1) 先把 LCD 点亮 —— 这样即使触摸/传感器有问题，也能看见画面，
 *      而且串口能看到卡在哪一步。
 *   2) 触摸做成"可选"：初始化失败就退化成按键操作，不阻塞游戏。
 *   3) MPU6050 用自己的一套软件 I2C（PB6/PB7），
 *      因为野火触摸的软件 I2C 是在 PH4/PH5 上，不能共用。
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

/* 触摸开关：
 *   屏幕触摸 IC 目前不通（GTP_Read_Version 一直重试，GTP_Init_Panel 不返回），
 *   所以先关掉，用 KEY1/KEY2 玩。等触摸调通把这个改成 1。 */
#define ENABLE_TOUCH    0

static int16_t g_ax_ref = 0;
static int16_t g_ay_ref = 0;

/* 触摸是否可用（初始化失败就只用按键） */
static uint8_t g_touch_ok = 0;

/* ---------------------------------------------------------------------------
 * 按键：KEY1 = PA0，KEY2 = PC13（板上已有下拉，按下为高）
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

/* 上升沿检测（调用间隔 20ms，本身就是消抖） */
static uint8_t key_edge(uint8_t now, uint8_t *last)
{
    uint8_t ev = (now && !(*last)) ? 1u : 0u;
    *last = now;
    return ev;
}

/* ---------------------------------------------------------------------------
 * 采加速度平均，作为"放平"的基准
 * ------------------------------------------------------------------------- */
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
    uint8_t  k1_last = 0, k2_last = 0;
    int32_t  touch_ret;

    /* ---------------- ① 基础 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();
    printf("\r\n=== F429 tilt ball game ===\r\n");

    SysTick_Init();             /* Delay_ms 用 */
    Tick_Init();                /* TIM6 -> 1ms 节拍 */
    Key_Init();

    /* ---------------- ② 先点屏（最重要，先让它亮） ---------------- */
    LCD_Init();
    LCD_LayerInit();
    LTDC_Cmd(ENABLE);

    LCD_SetLayer(LCD_BACKGROUND_LAYER);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetLayer(LCD_FOREGROUND_LAYER);
    LCD_SetTransparency(0xFF);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetFont(&LCD_DEFAULT_FONT);

    printf("LCD ready (cur_lcd=%d)\r\n", (int)cur_lcd);

    /* ---------------- ②.5 屏幕自检：红→绿→蓝→黑 ----------------
     * 这一步把"硬件"和"软件"分开：
     *   整屏能变成纯红/纯绿/纯蓝  -> SDRAM + LTDC + 时序 全部正常
     *   仍然是花屏/杂色           -> 问题在 SDRAM 或 LTDC 时序，跟绘制代码无关
     * ------------------------------------------------------------------ */
    printf("LCD self-test: RED...\r\n");
    LCD_Clear(LCD_COLOR_RED);    Delay_ms(600);
    printf("LCD self-test: GREEN...\r\n");
    LCD_Clear(LCD_COLOR_GREEN);  Delay_ms(600);
    printf("LCD self-test: BLUE...\r\n");
    LCD_Clear(LCD_COLOR_BLUE);   Delay_ms(600);
    printf("LCD self-test: WHITE...\r\n");
    LCD_Clear(LCD_COLOR_WHITE);  Delay_ms(600);
    LCD_Clear(LCD_COLOR_BLACK);
    printf("LCD self-test done.\r\n");

    /* ---------------- ③ 触摸（可失败） ---------------- */
#if ENABLE_TOUCH
    touch_ret  = GTP_Init_Panel();
    g_touch_ok = (touch_ret < 0) ? 0u : 1u;
    printf("touch init ret=%ld -> %s\r\n",
           (long)touch_ret, g_touch_ok ? "OK" : "FAILED, use KEY1/KEY2");
#else
    touch_ret  = -1;
    g_touch_ok = 0;
    printf("touch DISABLED (use KEY1/KEY2)\r\n");
#endif

    /* ---------------- ④ MPU6050 ---------------- */
    /* 注意：一定要先 MPU_Init（里面会配 PB6/PB7 的 GPIO），再去读寄存器。
     * 反过来写的话，引脚还是复位默认状态，拉不起来，读回 0x00。 */
    {
        MPU_Status st = MPU_Init(&g_imu);

        if (st == MPU_ERR_WHOAMI)
        {
            (void)MPU_ReadWhoAmI(&id);      /* 只是为了把 id 打出来看 */
            printf("IMU WHO_AM_I mismatch (got 0x%02X, expect 0x68)\r\n",
                   (unsigned)id);
            LED_RED;
            while (1) { }
        }
        else if (st != MPU_OK)
        {
            printf("IMU I2C error (st=%d)\r\n", (int)st);
            LED_RED;
            while (1) { }
        }

        (void)MPU_ReadWhoAmI(&id);
        printf("IMU WHO_AM_I = 0x%02X  OK\r\n", (unsigned)id);
    }

    printf("calibrating: keep the board FLAT and STILL ...\r\n");
    MPU_CalibrateGyro(&g_imu, 100);
    CalibrateLevel(100);
    printf("gyro bias = (%d, %d, %d)\r\n",
           (int)g_imu.gyro_bias[0], (int)g_imu.gyro_bias[1], (int)g_imu.gyro_bias[2]);
    printf("level ref = ax:%d ay:%d\r\n", (int)g_ax_ref, (int)g_ay_ref);
    printf("ready.\r\n\r\n");

    /* ---------------- ⑤ 游戏 ---------------- */
    printf("init game ...\r\n");
    App_Init(&g_app);
    printf("render first frame ...\r\n");
    Render_Begin(&g_app);
    printf("game ready: state=%s level=%u\r\n",
           App_StateName(&g_app), (unsigned)(g_app.game.level_index + 1));

    LED_BLUE;

    /* ---------------- 主循环 ---------------- */
    while (1)
    {
        uint32_t now = Tick_Get();

        /* ===== 每 20ms：按键 + 姿态 + 渲染 ===== */
        if ((uint32_t)(now - last_ctrl) >= 20)
        {
            uint32_t dtms = (uint32_t)(now - last_ctrl);
            MPU_Raw  d;
            uint8_t  k1, k2;

            last_ctrl = now;

            /* ---- 按键作输入（触摸可用时两者并存）---- */
            k1 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0)  == Bit_SET) ? 1u : 0u;
            k2 = (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) == Bit_SET) ? 1u : 0u;

            if (key_edge(k1, &k1_last) && g_app.button_count > 0)
            {
                App_DoAction(&g_app, g_app.buttons[0].action);
                printf("[KEY1] -> %s\r\n", App_StateName(&g_app));
                Render_Begin(&g_app);
            }
            if (key_edge(k2, &k2_last))
            {
                App_DoAction(&g_app, ACT_RESTART);
                printf("[KEY2] -> %s\r\n", App_StateName(&g_app));
                Render_Begin(&g_app);
            }

            /* ---- 姿态 ---- */
            if (MPU_ReadRaw(&g_imu, &d) == MPU_OK)
            {
                float tx = (float)((int16_t)(d.ax - g_ax_ref));
                float ty = (float)((int16_t)(d.ay - g_ay_ref));

                App_Update(&g_app, tx, ty, (float)dtms / 1000.0f);
            }

            Render_Frame(&g_app);

            /* ---- 灯 ---- */
            switch (g_app.game.state)
            {
            case GAME_RUNNING: LED_BLUE;   break;
            case GAME_WIN:     LED_GREEN;  break;
            case GAME_FAIL:    LED_RED;    break;
            case GAME_PAUSED:  LED_YELLOW; break;
            default:           LED_WHITE;  break;
            }
        }

        /* ===== 每 50ms：触摸（初始化成功时） ===== */
        if (g_touch_ok && (uint32_t)(now - last_touch) >= 50)
        {
            int x = 0, y = 0;
            int touching;

            last_touch = now;
            touching = GTP_Execu(&x, &y);

            if (touching && !was_touching)
            {
                if (App_HandleTap(&g_app, (int16_t)x, (int16_t)y))
                {
                    printf("tap (%d,%d) -> %s\r\n", x, y, App_StateName(&g_app));
                    Render_Begin(&g_app);
                }
            }
            was_touching = (uint8_t)(touching ? 1 : 0);
        }
    }
}
/*********************************************END OF FILE**********************/
