/**
 * @file    preview_adapter.c
 * @brief   把这个姿态游戏接到 PCFbView 的适配器（编译成 game_adapter.dll）
 *
 * PCFbView 只认 5 个函数：Pv_Attach / Pv_Update / Pv_Render / Pv_Key / Pv_Title
 * 这里把它们转给 game.c / app.c / render.c —— 那三个文件一个字都没改。
 */
#ifndef FBVIEW_BUILD_DLL
#define FBVIEW_BUILD_DLL
#endif
#include "fbview.h"

#include <stdio.h>
#include <string.h>

#include "render.h"
#include "pc_bsp_lcd.h"

uint16_t *g_fv_fb = NULL;
int       g_fv_w  = 0;
int       g_fv_h  = 0;

static App_Ctx s_app;
static char    s_title[192];

/* ---------------- 初始化 ---------------- */
int Pv_Attach(const FvHost *host)
{
    if (host == NULL || host->fb == NULL) { return -1; }

    g_fv_fb = host->fb;
    g_fv_w  = host->w;
    g_fv_h  = host->h;

    Pc_SetHost(host);

    printf("tilt game adapter: %dx%d, %d levels\n",
           host->w, host->h, (int)GAME_LEVEL_COUNT);

    App_Init(&s_app);
    Render_Begin(&s_app);
    return 0;
}

/* ---------------- 推进一帧 ---------------- */
void Pv_Update(float tilt_x, float tilt_y, float dt)
{
    /* Host input uses screen directions; firmware uses calibrated sensor axes. */
    App_Update(&s_app, tilt_x / GAME_SIGN_X, tilt_y / GAME_SIGN_Y, dt);
}

/* ---------------- 画一帧 ---------------- */
void Pv_Render(void)
{
    Render_Frame(&s_app);
    Pc_FlushText();
}

/* ---------------- 按键 ----------------
 * 1 = KEY1（等价于点当前第一个按钮）
 * 2 = KEY2（重玩本关）
 */
void Pv_Key(int key)
{
    if (key == 1)
    {
        if (s_app.button_count > 0)
        {
            App_DoAction(&s_app, s_app.buttons[0].action);
            Render_Begin(&s_app);
        }
    }
    else if (key == 2)
    {
        App_DoAction(&s_app, ACT_RESTART);
        Render_Begin(&s_app);
    }
}

/* ---------------- 标题栏 ---------------- */
const char *Pv_Title(void)
{
    sprintf(s_title, "LEVEL %u/%u   %s   %lu.%lus   WALL %u",
            (unsigned)(s_app.game.level_index + 1),
            (unsigned)GAME_LEVEL_COUNT,
            App_StateName(&s_app),
            (unsigned long)(s_app.game.elapsed_ms / 1000u),
            (unsigned long)((s_app.game.elapsed_ms % 1000u) / 100u),
            (unsigned)Game_GetLevel(&s_app.game)->wall_count);
    return s_title;
}
/* Optional host touch input, in native LCD pixel coordinates. */
void Pv_Touch(int x, int y, int pressed)
{
    if (pressed == 0 && App_HandleTap(&s_app, (int16_t)x, (int16_t)y))
        Render_Begin(&s_app);
}
