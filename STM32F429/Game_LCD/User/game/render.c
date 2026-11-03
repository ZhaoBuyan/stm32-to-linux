/**
 ******************************************************************************
 * @file    render.c
 * @brief   用野火的 LCD_* API 把游戏画出来
 *
 * 颜色约定：
 *   背景黑  墙灰  目标绿  红区红  球白  按钮蓝
 ******************************************************************************
 */
#include "render.h"
#include "../lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>

#define COL_BG      LCD_COLOR_BLACK
#define COL_WALL    LCD_COLOR_GREY
#define COL_TARGET  LCD_COLOR_GREEN
#define COL_HAZARD  LCD_COLOR_RED
#define COL_BALL    LCD_COLOR_WHITE
#define COL_BTN     LCD_COLOR_BLUE
#define COL_TEXT    LCD_COLOR_WHITE

/* 上一帧球心，用来擦除 */
static int16_t s_prev_x = -1000;
static int16_t s_prev_y = -1000;

/* 上一次画过的状态，用来判断要不要重画整屏 */
static Game_State s_prev_state = (Game_State)0xFF;
static uint8_t    s_prev_level = 0xFF;

/* ----------------------------------------------------------------------------
 * 小工具
 * -------------------------------------------------------------------------- */

static void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color)
{
    if (w <= 0 || h <= 0) { return; }
    LCD_SetTextColor(color);
    LCD_DrawFullRect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
}

static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint32_t color)
{
    LCD_SetTextColor(color);
    LCD_DrawFullCircle((uint16_t)cx, (uint16_t)cy, (uint16_t)r);
}

/* 两个矩形求交，返回 1 表示有交集 */
static uint8_t rect_intersect(const Game_Rect *a, const Game_Rect *b, Game_Rect *out)
{
    int16_t x0 = (a->x > b->x) ? a->x : b->x;
    int16_t y0 = (a->y > b->y) ? a->y : b->y;
    int16_t x1 = ((int16_t)(a->x + a->w) < (int16_t)(b->x + b->w)) ? (int16_t)(a->x + a->w)
                                                                  : (int16_t)(b->x + b->w);
    int16_t y1 = ((int16_t)(a->y + a->h) < (int16_t)(b->y + b->h)) ? (int16_t)(a->y + a->h)
                                                                  : (int16_t)(b->y + b->h);

    if (x1 <= x0 || y1 <= y0) { return 0; }

    out->x = x0; out->y = y0;
    out->w = (int16_t)(x1 - x0);
    out->h = (int16_t)(y1 - y0);
    return 1;
}

/* 把矩形裁到指定范围内，返回 1 表示裁剪后非空 */
static uint8_t rect_clip(Game_Rect *r, int16_t cx0, int16_t cy0,
                         int16_t cx1, int16_t cy1)
{
    int16_t x0 = (r->x > cx0) ? r->x : cx0;
    int16_t y0 = (r->y > cy0) ? r->y : cy0;
    int16_t x1 = ((int16_t)(r->x + r->w) < cx1) ? (int16_t)(r->x + r->w) : cx1;
    int16_t y1 = ((int16_t)(r->y + r->h) < cy1) ? (int16_t)(r->y + r->h) : cy1;

    if (x1 <= x0 || y1 <= y0) { return 0; }

    r->x = x0; r->y = y0;
    r->w = (int16_t)(x1 - x0);
    r->h = (int16_t)(y1 - y0);
    return 1;
}

/* ----------------------------------------------------------------------------
 * 静态元素
 * -------------------------------------------------------------------------- */

static void draw_scene_rects(App_Ctx *a)
{
    const Game_Level *L = Game_GetLevel(&a->game);
    uint8_t i;

    for (i = 0; i < L->wall_count; i++)
    {
        fill(L->walls[i].x, L->walls[i].y, L->walls[i].w, L->walls[i].h, COL_WALL);
    }

    if (L->hazard_on)
    {
        fill(L->hazard.x, L->hazard.y, L->hazard.w, L->hazard.h, COL_HAZARD);
    }

    fill(L->target.x, L->target.y, L->target.w, L->target.h, COL_TARGET);
}

static void draw_buttons(App_Ctx *a);   /* 前置声明，下面用到 */

/* 用 LCD_DisplayChar 在任意位置画字符串（Line/Column 是字符格坐标） */
static void draw_text_at(int16_t x, int16_t y, const char *s, uint32_t color, uint32_t bg)
{
    LCD_SetTextColor(color);
    LCD_SetBackColor(bg);

    /* LCD_DisplayChar 的 Line/Column 是按当前字体算的字符格 */
    {
        uint16_t line = (uint16_t)(y / 24);            /* 默认 Font16x24 */
        uint16_t col  = (uint16_t)(x / 16);
        while (*s)
        {
            LCD_DisplayChar(line, col, (uint8_t)(*s));
            col++;
            s++;
        }
    }
}

static void draw_buttons(App_Ctx *a)
{
    uint8_t n, i;
    const App_Button *bs = App_Buttons(a, &n);

    for (i = 0; i < n; i++)
    {
        fill(bs[i].rect.x, bs[i].rect.y, bs[i].rect.w, bs[i].rect.h, COL_BTN);
        draw_text_at((int16_t)(bs[i].rect.x + 20),
                     (int16_t)(bs[i].rect.y + 20),
                     bs[i].label, COL_TEXT, COL_BTN);
    }
}

static void draw_hud(App_Ctx *a)
{
    char buf[64];
    const Game_Level *L = Game_GetLevel(&a->game);

    /* 状态栏背景 */
    fill(0, 0, GAME_SCREEN_W, GAME_PLAY_TOP, COL_BG);

    sprintf(buf, "LEVEL %u/%u   %s",
            (unsigned)(a->game.level_index + 1),
            (unsigned)GAME_LEVEL_COUNT,
            App_StateName(a));
    draw_text_at(8, 4, buf, COL_TEXT, COL_BG);

    sprintf(buf, "TIME %lu ms   WALL %u",
            (unsigned long)a->game.elapsed_ms,
            (unsigned)L->wall_count);
    draw_text_at(8, 26, buf, COL_TEXT, COL_BG);
}

/* ----------------------------------------------------------------------------
 * 擦除球之后，把被覆盖的静态元素补回来
 * -------------------------------------------------------------------------- */

static void restore_under_ball(App_Ctx *a, int16_t cx, int16_t cy)
{
    const Game_Level *L = Game_GetLevel(&a->game);
    Game_Rect box, hit;
    uint8_t i;
    uint8_t n;
    const App_Button *bs;

    box.x = (int16_t)(cx - GAME_BALL_R);
    box.y = (int16_t)(cy - GAME_BALL_R);
    box.w = (int16_t)(GAME_BALL_R * 2);
    box.h = (int16_t)(GAME_BALL_R * 2);

    for (i = 0; i < L->wall_count; i++)
    {
        if (rect_intersect(&L->walls[i], &box, &hit))
        {
            fill(hit.x, hit.y, hit.w, hit.h, COL_WALL);
        }
    }

    if (L->hazard_on && rect_intersect(&L->hazard, &box, &hit))
    {
        fill(hit.x, hit.y, hit.w, hit.h, COL_HAZARD);
    }

    if (rect_intersect(&L->target, &box, &hit))
    {
        fill(hit.x, hit.y, hit.w, hit.h, COL_TARGET);
    }

    /* 按钮被擦到也要补 */
    bs = App_Buttons(a, &n);
    for (i = 0; i < n; i++)
    {
        if (rect_intersect(&bs[i].rect, &box, &hit))
        {
            Game_Rect clip = bs[i].rect;
            if (rect_clip(&clip, hit.x, hit.y,
                          (int16_t)(hit.x + hit.w), (int16_t)(hit.y + hit.h)))
            {
                fill(clip.x, clip.y, clip.w, clip.h, COL_BTN);
            }
        }
    }
}

/* ----------------------------------------------------------------------------
 * 对外接口
 * -------------------------------------------------------------------------- */

void Render_Begin(App_Ctx *a)
{
    if (a == 0) { return; }

    LCD_Clear(COL_BG);                  /* 整屏刷黑 */
    draw_scene_rects(a);                /* 墙 / 红区 / 目标 */
    draw_hud(a);                        /* 状态栏 */
    draw_buttons(a);                    /* 当前状态的按钮 */

    /* 球 */
    fill_circle((int16_t)a->game.ball.x, (int16_t)a->game.ball.y,
                GAME_BALL_R, COL_BALL);

    s_prev_x     = (int16_t)a->game.ball.x;
    s_prev_y     = (int16_t)a->game.ball.y;
    s_prev_state = a->game.state;
    s_prev_level = a->game.level_index;
}

void Render_Frame(App_Ctx *a)
{
    int16_t cx, cy;

    if (a == 0) { return; }

    cx = (int16_t)a->game.ball.x;
    cy = (int16_t)a->game.ball.y;

    /* 关卡或状态变化 —— 静态层要重画 */
    if (a->game.state != s_prev_state || a->game.level_index != s_prev_level)
    {
        Render_Begin(a);
        return;
    }

    /* 球没动就别画（省 SDRAM 带宽） */
    if (cx == s_prev_x && cy == s_prev_y) { return; }

    /* ① 用背景色擦掉旧球 */
    fill_circle(s_prev_x, s_prev_y, GAME_BALL_R, COL_BG);

    /* ② 旧球位置可能压在墙/目标/按钮上，补回来 */
    restore_under_ball(a, s_prev_x, s_prev_y);

    /* ③ 画新球 */
    fill_circle(cx, cy, GAME_BALL_R, COL_BALL);

    s_prev_x = cx;
    s_prev_y = cy;
}
