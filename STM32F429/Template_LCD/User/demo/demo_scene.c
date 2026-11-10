/**
 ******************************************************************************
 * @file    demo_scene.c
 * @brief   模板工程的演示画面
 *
 * 画的东西不多，但每个 gfx 原语都用到了，方便你改的时候有参照：
 *   Gfx_Clear          整屏
 *   Gfx_Fill           色条
 *   Gfx_FillRoundBorder 圆角面板
 *   Gfx_FillCircle     运动的小球
 *   Gfx_Line           折线
 *   Gfx_Text           标题 / 状态文字
 ******************************************************************************
 */
#include "demo_scene.h"
#include <stdio.h>
#include "../gfx/gfx.h"

/* ---------------- 配色 ---------------- */
#define COL_BG        0x10141C
#define COL_PANEL     0x1A2230
#define COL_PANEL_ED  0x2A3442
#define COL_TITLE     0xE6EDF3
#define COL_DIM       0x8B98A9
#define COL_ACCENT    0x4A90D9
#define COL_BALL      0x35D07F
#define COL_WARN      0xFF4D5E

/* 三角形波：在 [lo, hi] 之间来回走，不用浮点 */
static int16_t tri(uint32_t t, int16_t lo, int16_t hi, uint32_t period)
{
    uint32_t p, half;

    if (period < 2) { period = 2; }
    half = period / 2;
    p = t % period;

    if (p < half)
    {
        return (int16_t)(lo + (int32_t)(hi - lo) * (int32_t)p / (int32_t)half);
    }
    return (int16_t)(hi - (int32_t)(hi - lo) * (int32_t)(p - half) / (int32_t)half);
}

void Demo_Draw(uint32_t now_ms, int key1, int key2)
{
    char     buf[64];
    int16_t  cx, cy;
    int16_t  i;
    static const uint32_t bar[6] =
    {
        0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF
    };

    /* ① 背景 */
    Gfx_Clear(COL_BG);

    /* ② 顶部标题 */
    Gfx_Text(16, 12, "Template_LCD", COL_TITLE, COL_BG);
    Gfx_Text(200, 12, "gfx primitives demo", COL_DIM, COL_BG);

    /* ③ 色条：验证 RGB565 通道没接反（红绿蓝必须和标签一致） */
    for (i = 0; i < 6; i++)
    {
        Gfx_Fill((int16_t)(16 + i * 40), 48, 38, 20, bar[i]);
    }
    Gfx_Text(268, 46, "RGB bars", COL_DIM, COL_BG);

    /* ④ 圆角面板（带描边） */
    Gfx_FillRoundBorder(16, 92, 300, 120, 14, COL_PANEL, COL_PANEL_ED);
    Gfx_Text(36, 108, "Gfx_FillRoundBorder", COL_DIM, COL_PANEL);
    Gfx_Text(36, 140, "rounded rect + 2px edge", COL_TITLE, COL_PANEL);

    /* ⑤ 折线（验证 Gfx_Line 不会画斜）*/
    Gfx_Text(16, 232, "Gfx_Line:", COL_DIM, COL_BG);
    {
        int16_t base_y = 300;
        for (i = 0; i < 8; i++)
        {
            int16_t x0 = (int16_t)(100 + i * 40);
            int16_t x1 = (int16_t)(100 + (i + 1) * 40);
            int16_t y0 = (int16_t)(base_y - ((i & 1) ? 40 : 0));
            int16_t y1 = (int16_t)(base_y - (((i + 1) & 1) ? 40 : 0));
            Gfx_Line(x0, y0, x1, y1, COL_ACCENT);
        }
    }

    /* ⑥ 运动的小球（画完和上一帧没关系，这里每帧整屏重画，简单点）*/
    cx = tri(now_ms * 2, 60, 740, 6000);
    cy = tri(now_ms * 3, 120, 420, 4000);
    Gfx_FillCircle(cx, cy, 18, COL_BALL);
    Gfx_FillCircle((int16_t)(cx - 5), (int16_t)(cy - 5), 5, 0x8FF0B8);

    /* ⑦ 按键状态（按键由 main.c 传入，PC 预览器由适配器传入）*/
    sprintf(buf, "KEY1=%d  KEY2=%d", key1, key2);
    Gfx_Text(16, 440, buf, (key1 || key2) ? COL_WARN : COL_DIM, COL_BG);

    sprintf(buf, "t = %lu ms", (unsigned long)now_ms);
    Gfx_Text(300, 440, buf, COL_DIM, COL_BG);

    Gfx_Text(560, 440, "800x480 RGB565", COL_DIM, COL_BG);
}

