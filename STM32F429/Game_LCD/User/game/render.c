/**
 ******************************************************************************
 * @file    render.c
 * @brief   把游戏画到 800x480 RGB565 显存上
 *
 * 设计原则：
 *   1. 所有像素由 CPU 直接写显存，不用 DMA2D —— 避免和 LTDC 抢 SDRAM
 *   2. 静态层（背景/墙/目标/按钮/HUD）只在状态或关卡变化时重画一次
 *   3. 每帧只擦旧球、补被压到的静态元素、画新球
 *
 * 配色走深色主题，避免"红白机"那种高纯度原色。
 ******************************************************************************
 */
#include "render.h"
#include "../lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>

/* ---------------- 配色 ---------------- */
#define COL_BG          0x10141C        /* 背景：深蓝黑 */
#define COL_HUD_BG      0x0A0E14        /* 状态栏底色，比背景更暗 */
#define COL_HUD_LINE    0x2A3442        /* 状态栏分隔线 */
#define COL_TEXT        0xE6EDF3        /* 主文字：近白 */
#define COL_TEXT_DIM    0x8B98A9        /* 次要文字 */

#define COL_WALL        0x2E3947        /* 墙体填充：石板灰 */
#define COL_WALL_EDGE   0x4A5C70        /* 墙体描边 */

#define COL_TARGET_IN   0x14432E        /* 目标区填充：暗绿 */
#define COL_TARGET_ED   0x35D07F        /* 目标区描边：荧光绿 */

#define COL_HAZARD_IN   0x4A1420        /* 危险区填充：暗红 */
#define COL_HAZARD_ED   0xFF4D5E        /* 危险区描边：亮红 */

#define COL_BALL        0xFFFFFF
#define COL_BALL_EDGE   0x9FC4EE        /* 球边缘一圈淡蓝，有点立体感 */

#define COL_BTN         0x1F3A5F        /* 按钮填充：深蓝 */
#define COL_BTN_EDGE    0x4A90D9        /* 按钮描边 */

/* ---------------- 显存 ---------------- */
/* 野火双层显存：前景层 = 基址 + BUFFER_OFFSET（main 里切的是前景层） */
#define FB_BASE     ((volatile uint8_t *)(LCD_FRAME_BUFFER + BUFFER_OFFSET))

/* ---------------- 上一帧状态（用于局部刷新） ---------------- */
static int16_t    s_prev_x     = -1000;
static int16_t    s_prev_y     = -1000;
static Game_State s_prev_state = (Game_State)0xFF;
static uint8_t    s_prev_level = 0xFF;

/* ============================================================================
 * 基础绘制原语（直接写显存）
 * ========================================================================== */

/* 24 位色 0xRRGGBB -> RGB565。
 * 注意：RGB565 分支下野火的 LCD_SetTextColor/SetBackColor 参数就是 uint16_t，
 * 必须传打包好的 565 值，直接传 24 位会被截断成错误颜色。 */
static uint16_t to565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 0xF8u) << 8) |
                      (((c >> 8)  & 0xFCu) << 3) |
                      (( c        & 0xF8u) >> 3));
}

static void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color)
{
    uint16_t c565 = to565(color);
    uint8_t lo = (uint8_t)(c565 & 0xFF);
    uint8_t hi = (uint8_t)(c565 >> 8);
    int16_t i, j;

    if (x < 0) { w = (int16_t)(w + x); x = 0; }
    if (y < 0) { h = (int16_t)(h + y); y = 0; }
    if ((int32_t)x + w > GAME_SCREEN_W) { w = (int16_t)(GAME_SCREEN_W - x); }
    if ((int32_t)y + h > GAME_SCREEN_H) { h = (int16_t)(GAME_SCREEN_H - y); }
    if (w <= 0 || h <= 0) { return; }

    for (j = 0; j < h; j++)
    {
        volatile uint8_t *p = FB_BASE + 2u * ((uint32_t)(y + j) * LCD_PIXEL_WIDTH + (uint32_t)x);
        for (i = 0; i < w; i++)
        {
            p[0] = lo;
            p[1] = hi;
            p += 2;
        }
    }
}

static int16_t isqrt16(uint32_t v)
{
    int16_t h = 0;
    while ((uint32_t)(h + 1) * (uint32_t)(h + 1) <= v) { h++; }
    return h;
}

/* 圆角矩形：逐行算出左右各内缩多少 */
static void fill_round(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint32_t color)
{
    int16_t j;

    if (r > w / 2) { r = (int16_t)(w / 2); }
    if (r > h / 2) { r = (int16_t)(h / 2); }
    if (r < 0) { r = 0; }

    for (j = 0; j < h; j++)
    {
        int16_t inset = 0;

        if (j < r)
        {
            int16_t d = (int16_t)(r - j);
            inset = (int16_t)(r - isqrt16((uint32_t)(r * r - d * d)));
        }
        else if (j > (int16_t)(h - 1 - r))
        {
            int16_t d = (int16_t)(j - (h - 1 - r));
            inset = (int16_t)(r - isqrt16((uint32_t)(r * r - d * d)));
        }

        fill((int16_t)(x + inset), (int16_t)(y + j),
             (int16_t)(w - inset * 2), 1, color);
    }
}

/* 圆角矩形 + 描边（先画外框色，再画内芯）*/
static void fill_round_border(int16_t x, int16_t y, int16_t w, int16_t h,
                              int16_t r, uint32_t in_color, uint32_t edge_color)
{
    fill_round(x, y, w, h, r, edge_color);
    fill_round((int16_t)(x + 2), (int16_t)(y + 2),
               (int16_t)(w - 4), (int16_t)(h - 4),
               (int16_t)(r > 2 ? r - 2 : 0), in_color);
}

/* 实心圆 */
static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint32_t color)
{
    int16_t dy;

    for (dy = -r; dy <= r; dy++)
    {
        int32_t w2 = (int32_t)r * r - (int32_t)dy * dy;
        int16_t half = (w2 > 0) ? isqrt16((uint32_t)w2) : 0;

        fill((int16_t)(cx - half), (int16_t)(cy + dy),
             (int16_t)(half * 2 + 1), 1, color);
    }
}

/* ---------------- 文字 ---------------- */
/* 野火的 LCD_DisplayChar 参数顺序是【纵像素, 横像素】*/
#define FONT_W      16

static void text_at(int16_t x, int16_t y, const char *s, uint32_t color, uint32_t bg)
{
    LCD_SetTextColor(to565(color));
    LCD_SetBackColor(to565(bg));

    while (*s)
    {
        LCD_DisplayChar((uint16_t)y, (uint16_t)x, (uint8_t)(*s));
        x = (int16_t)(x + FONT_W);
        s++;
    }
}

/* ============================================================================
 * 静态层
 * ========================================================================== */

static uint8_t rect_intersect(const Game_Rect *a, const Game_Rect *b, Game_Rect *out)
{
    int16_t x0 = (a->x > b->x) ? a->x : b->x;
    int16_t y0 = (a->y > b->y) ? a->y : b->y;
    int16_t x1 = ((int16_t)(a->x + a->w) < (int16_t)(b->x + b->w))
                 ? (int16_t)(a->x + a->w) : (int16_t)(b->x + b->w);
    int16_t y1 = ((int16_t)(a->y + a->h) < (int16_t)(b->y + b->h))
                 ? (int16_t)(a->y + a->h) : (int16_t)(b->y + b->h);

    if (x1 <= x0 || y1 <= y0) { return 0; }

    out->x = x0; out->y = y0;
    out->w = (int16_t)(x1 - x0);
    out->h = (int16_t)(y1 - y0);
    return 1;
}

static void draw_hud(App_Ctx *a)
{
    char buf[48];
    const Game_Level *L = Game_GetLevel(&a->game);

    /* 状态栏底色 + 底部一条分隔线 */
    fill(0, 0, GAME_SCREEN_W, GAME_PLAY_TOP, COL_HUD_BG);
    fill(0, (int16_t)(GAME_PLAY_TOP - 2), GAME_SCREEN_W, 2, COL_HUD_LINE);

    sprintf(buf, "LEVEL %u/%u", (unsigned)(a->game.level_index + 1),
            (unsigned)GAME_LEVEL_COUNT);
    text_at(16, 14, buf, COL_TEXT, COL_HUD_BG);

    sprintf(buf, "%lu.%lus", (unsigned long)(a->game.elapsed_ms / 1000u),
            (unsigned long)((a->game.elapsed_ms % 1000u) / 100u));
    text_at(210, 14, buf, COL_TEXT_DIM, COL_HUD_BG);

    sprintf(buf, "WALL %u", (unsigned)L->wall_count);
    text_at(360, 14, buf, COL_TEXT_DIM, COL_HUD_BG);

    text_at(620, 14, App_StateName(a), COL_TEXT, COL_HUD_BG);
}

static void draw_wall(const Game_Rect *r)
{
    fill_round_border(r->x, r->y, r->w, r->h, 6, COL_WALL, COL_WALL_EDGE);
}

static void draw_target(const Game_Rect *r)
{
    fill_round_border(r->x, r->y, r->w, r->h, 10, COL_TARGET_IN, COL_TARGET_ED);

    /* 中间画个小十字，提示"到这来" */
    {
        int16_t cx = (int16_t)(r->x + r->w / 2);
        int16_t cy = (int16_t)(r->y + r->h / 2);
        fill((int16_t)(cx - 10), cy, 20, 2, COL_TARGET_ED);
        fill(cx, (int16_t)(cy - 10), 2, 20, COL_TARGET_ED);
    }
}

static void draw_hazard(const Game_Rect *r)
{
    fill_round_border(r->x, r->y, r->w, r->h, 10, COL_HAZARD_IN, COL_HAZARD_ED);

    /* 中间画个 X，一眼看出是"别碰" */
    {
        int16_t i;
        int16_t cx = (int16_t)(r->x + r->w / 2);
        int16_t cy = (int16_t)(r->y + r->h / 2);
        int16_t n  = (int16_t)(((r->w < r->h) ? r->w : r->h) / 6);

        for (i = -n; i <= n; i++)
        {
            fill((int16_t)(cx + i), (int16_t)(cy + i), 2, 2, COL_HAZARD_ED);
            fill((int16_t)(cx + i), (int16_t)(cy - i), 2, 2, COL_HAZARD_ED);
        }
    }
}

static void draw_button(const App_Button *b)
{
    int16_t len = (int16_t)strlen(b->label);
    int16_t tx  = (int16_t)(b->rect.x + (b->rect.w - len * FONT_W) / 2);
    int16_t ty  = (int16_t)(b->rect.y + (b->rect.h - 24) / 2);

    fill_round_border(b->rect.x, b->rect.y, b->rect.w, b->rect.h,
                      12, COL_BTN, COL_BTN_EDGE);
    text_at(tx, ty, b->label, COL_TEXT, COL_BTN);
}


static void draw_ball(int16_t cx, int16_t cy)
{
    fill_circle(cx, cy, GAME_BALL_R,     COL_BALL_EDGE);   /* 外圈淡蓝当描边 */
    fill_circle(cx, cy, GAME_BALL_R - 2, COL_BALL);        /* 内芯纯白 */
    fill_circle((int16_t)(cx - 3), (int16_t)(cy - 3), 2, COL_BALL_EDGE); /* 高光 */
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */

void Render_Begin(App_Ctx *a)
{
    const Game_Level *L;
    uint8_t i;
    uint8_t n;
    const App_Button *bs;

    if (a == 0) { return; }

    L = Game_GetLevel(&a->game);

    /* 背景 */
    fill(0, 0, GAME_SCREEN_W, GAME_SCREEN_H, COL_BG);

    /* 场景 */
    for (i = 0; i < L->wall_count; i++) { draw_wall(&L->walls[i]); }
    if (L->hazard_on) { draw_hazard(&L->hazard); }
    draw_target(&L->target);

    /* HUD 与按钮 */
    draw_hud(a);
    bs = App_Buttons(a, &n);
    for (i = 0; i < n; i++) { draw_button(&bs[i]); }

    /* 球 */
    draw_ball((int16_t)a->game.ball.x, (int16_t)a->game.ball.y);

    s_prev_x     = (int16_t)a->game.ball.x;
    s_prev_y     = (int16_t)a->game.ball.y;
    s_prev_state = a->game.state;
    s_prev_level = a->game.level_index;
}

/* 擦球之后，把被它压到的静态元素补回来 */
static void restore_under(App_Ctx *a, int16_t cx, int16_t cy)
{
    const Game_Level *L = Game_GetLevel(&a->game);
    Game_Rect box, hit;
    uint8_t i, n;
    const App_Button *bs;

    box.x = (int16_t)(cx - GAME_BALL_R);
    box.y = (int16_t)(cy - GAME_BALL_R);
    box.w = (int16_t)(GAME_BALL_R * 2);
    box.h = (int16_t)(GAME_BALL_R * 2);

    /* 注意：这里只填【纯色】，不要再画圆角/描边/图案。
     * 因为 hit 是被球切过的一小块，按它重新画圆角或 X 图案会变形。 */
    for (i = 0; i < L->wall_count; i++)
    {
        if (rect_intersect(&L->walls[i], &box, &hit))
        {
            fill(hit.x, hit.y, hit.w, hit.h, COL_WALL);
        }
    }
    if (L->hazard_on && rect_intersect(&L->hazard, &box, &hit))
    {
        fill(hit.x, hit.y, hit.w, hit.h, COL_HAZARD_IN);
    }
    if (rect_intersect(&L->target, &box, &hit))
    {
        fill(hit.x, hit.y, hit.w, hit.h, COL_TARGET_IN);
    }

    /* 按钮小，直接整个重画（带文字）更保险 */
    bs = App_Buttons(a, &n);
    for (i = 0; i < n; i++)
    {
        if (rect_intersect(&bs[i].rect, &box, &hit)) { draw_button(&bs[i]); }
    }
}

void Render_Frame(App_Ctx *a)
{
    int16_t cx, cy;

    if (a == 0) { return; }

    cx = (int16_t)a->game.ball.x;
    cy = (int16_t)a->game.ball.y;

    /* 关卡或状态变了 —— 整屏重画 */
    if (a->game.state != s_prev_state || a->game.level_index != s_prev_level)
    {
        Render_Begin(a);
        return;
    }

    /* 球没动就不用画（省 SDRAM 带宽） */
    if (cx == s_prev_x && cy == s_prev_y) { return; }

    /* ① 擦旧球 */
    fill_circle(s_prev_x, s_prev_y, GAME_BALL_R, COL_BG);

    /* ② 补被擦到的静态元素 */
    restore_under(a, s_prev_x, s_prev_y);

    /* ③ 画新球 */
    draw_ball(cx, cy);

    s_prev_x = cx;
    s_prev_y = cy;
}
