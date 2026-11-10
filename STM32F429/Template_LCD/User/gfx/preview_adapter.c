/**
 ******************************************************************************
 * @file    preview_adapter.c
 * @brief   把 Template_LCD 接到 EmbeddedScreenPreview 的适配器
 *
 * 它做的就一件事：把宿主的帧缓冲和输入转给 Demo_Draw()。
 * 也就是说 —— **板子上和电脑上跑的是同一份绘制代码**。
 *
 * 编译成 game 那样的 DLL 后，预览器就能"选这个文件夹 → 显示画面"：
 *   EmbeddedScreenPreview.exe -dir <本工程目录>
 * 具体见同目录上级的 preview.ini。
 ******************************************************************************
 */
#ifndef FBVIEW_BUILD_DLL
#define FBVIEW_BUILD_DLL
#endif
#include "fbview.h"

#include <stdio.h>

#include "../demo/demo_scene.h"
#include "pc_bsp_lcd.h"

/* 由 pc_bsp_lcd.h 声明，这里定义（供 gfx.c 使用）*/
uint16_t *g_fv_fb = NULL;
int       g_fv_w  = 0;
int       g_fv_h  = 0;

static uint32_t s_now_ms = 0;
static char     s_title[128];

/* ---------------- 生命周期 ---------------- */

int Pv_Attach(const FvHost *host)
{
    if (host == NULL || host->fb == NULL) { return -1; }

    g_fv_fb = host->fb;
    g_fv_w  = host->w;
    g_fv_h  = host->h;

    /* 必须告诉 pc_bsp_lcd.c 宿主是谁，否则 Pc_FlushText() 会把文字丢掉 */
    Pc_SetHost(host);

    printf("Template_LCD adapter attached: %dx%d\n", host->w, host->h);
    return 0;
}

void Pv_Update(float tilt_x, float tilt_y, float dt)
{
    (void)tilt_x;
    (void)tilt_y;

    /* dt 是秒，转成毫秒累加，让 Demo_Draw 里的小球动起来 */
    if (dt > 0.0f) { s_now_ms += (uint32_t)(dt * 1000.0f + 0.5f); }
}

void Pv_Render(void)
{
    /* 和板子上完全相同的绘制调用 */
    Demo_Draw(s_now_ms, 0, 0);

    /* 把 Gfx_Text 收集到的字符交给宿主，用系统字体画出来。
     * 不调这句的表现是：文字格子的背景色有、字形没有。*/
    Pc_FlushText();
}

/* 按键 1 / 2 对应 KEY1 / KEY2 —— 板子上是 GPIO，这里是键盘 */
void Pv_Key(int key)
{
    printf("KEY%d\n", key);
}

const char *Pv_Title(void)
{
    sprintf(s_title, "Template_LCD   t=%lu ms   %dx%d",
            (unsigned long)s_now_ms, g_fv_w, g_fv_h);
    return s_title;
}
