/**
 ******************************************************************************
 * @file    game.c
 * @brief   倾斜控制小球 —— 物理、碰撞、胜负
 *
 * 碰撞处理说明（对应任务书"不得只依靠球心"）：
 *   球按【边长 2R 的方块】参与碰撞，也就是把球的整个占据范围算进去。
 *   只用球心判断的话，球会有一半嵌进墙里或提前判胜负。
 *
 * 墙体的推出策略：
 *   算出球和矩形在四个方向上的重叠量，选【最小的那个方向】推出去。
 *   这是 AABB 碰撞的标准做法，能避免"从角上穿过去"。
 ******************************************************************************
 */
#include "game.h"
#include <string.h>
#include <math.h>

#define LSB_PER_G   16384.0f

/* ============================================================================
 * 关卡数据
 *
 * 尺寸参考：屏幕 800x480，球半径 10 → 通道至少留 40px 才好过。
 * ========================================================================== */

static const Game_Level k_levels[GAME_LEVEL_COUNT] =
{
    /* ================= 第 1 关：空场，斜穿全屏 ----------------
     * 目标：先熟悉"倾斜 → 球滚"的手感，不能有障碍。
     * 起点左下，目标右上，要走完整条对角线。
     */
    {
        { { 0, 0, 0, 0 } }, 0,
        { 0, 0, 0, 0 }, 0,
        { 660, 60, 110, 90 },
        70.0f, 430.0f
    },

    /* ================= 第 2 关：一道横墙，从左边伸出来 ----------------
     * 新元素：墙。
     * 墙从 x=0 伸到 x=520，右边留 270px 通道 —— 必须绕右侧。
     */
    {
        {
            { 0, 240, 520, 30 }
        }, 1,
        { 0, 0, 0, 0 }, 0,
        { 660, 60, 110, 90 },
        70.0f, 430.0f
    },

    /* ================= 第 3 关：横墙从右边伸出 + 红区守左道 ----------------
     * 新元素：危险区。
     * 墙封住右侧，只能走左通道；而红区正好卡在左通道中间，
     * 得贴边走（左边 100px 缝 或 右边 80px 缝）。
     */
    {
        {
            { 300, 240, 500, 30 }
        }, 1,
        { 100, 170, 120, 80 }, 1,
        { 660, 60, 110, 90 },
        70.0f, 430.0f
    },

    /* ================= 第 4 关：竖墙 + 红区封下方通路 ----------------
     * 从左上出发，绕到墙下方，还要从红区上方 60px 的缝里钻过去。
     */
    {
        {
            { 380, 60, 30, 250 }
        }, 1,
        { 200, 390, 150, 60 }, 1,
        { 660, 400, 110, 70 },
        70.0f, 70.0f
    },

    /* ================= 第 5 关：两道竖墙，逼你上下换道 ----------------
     * 新元素：两段错开的墙。
     * 第一道封上半，第二道封下半 —— 必须先下、再上、再右。
     */
    {
        {
            { 240,  60, 30, 270 },
            { 520, 200, 30, 270 }
        }, 2,
        { 0, 0, 0, 0 }, 0,
        { 700, 60, 90, 90 },
        70.0f, 430.0f
    },

    /* ================= 第 6 关：综合 ----------------
     * 横墙封左上、竖墙封右侧，红区压在左下。
     * 路线：底部右行 → 绕过横墙右端 → 上升 → 绕过竖墙右端 → 到右上目标。
     */
    {
        {
            {   0, 200, 420, 30 },
            { 600,  60,  30, 250 }
        }, 2,
        { 100, 320, 200, 80 }, 1,
        { 690, 70, 90, 90 },
        70.0f, 430.0f
    }
};

/* 速度矢量限速 */
static void clamp_speed(Game_Ball *b)
{
    float sp2 = b->vx * b->vx + b->vy * b->vy;

    if (sp2 > GAME_MAX_SPEED * GAME_MAX_SPEED)
    {
        float k = GAME_MAX_SPEED / sqrtf(sp2);
        b->vx *= k;
        b->vy *= k;
    }
}

/* 屏幕四边（含球半径） */
static void bounce_screen(Game_Ball *b)
{
    if (b->x < GAME_BALL_R)
    {
        b->x = GAME_BALL_R;
        if (b->vx < 0.0f) { b->vx = -b->vx * GAME_BOUNCE; }
    }
    else if (b->x > GAME_SCREEN_W - GAME_BALL_R)
    {
        b->x = GAME_SCREEN_W - GAME_BALL_R;
        if (b->vx > 0.0f) { b->vx = -b->vx * GAME_BOUNCE; }
    }

    if (b->y < (float)(GAME_PLAY_TOP + GAME_BALL_R))
    {
        b->y = (float)(GAME_PLAY_TOP + GAME_BALL_R);
        if (b->vy < 0.0f) { b->vy = -b->vy * GAME_BOUNCE; }
    }
    else if (b->y > GAME_SCREEN_H - GAME_BALL_R)
    {
        b->y = GAME_SCREEN_H - GAME_BALL_R;
        if (b->vy > 0.0f) { b->vy = -b->vy * GAME_BOUNCE; }
    }
}

/* 球（按 2R x 2R 方块）与一个墙体的碰撞推出 */
static void resolve_wall(Game_Ball *b, const Game_Rect *w)
{
    float bl, br, bt, bb;       /* 球的范围 */
    float wl, wr, wt, wb;       /* 墙的范围 */
    float ox1, ox2, oy1, oy2;   /* 四个方向的重叠量 */
    float min_ov;
    uint8_t axis;

    if (w->w <= 0 || w->h <= 0) { return; }

    bl = b->x - GAME_BALL_R;  br = b->x + GAME_BALL_R;
    bt = b->y - GAME_BALL_R;  bb = b->y + GAME_BALL_R;

    wl = (float)w->x;                 wr = (float)(w->x + w->w);
    wt = (float)w->y;                 wb = (float)(w->y + w->h);

    /* 完全没重叠就直接返回 */
    if (br <= wl || bl >= wr || bb <= wt || bt >= wb) { return; }

    /* 从四个方向推出各需要移动多少 */
    ox1 = br - wl;      /* 往左推 */
    ox2 = wr - bl;      /* 往右推 */
    oy1 = bb - wt;      /* 往上推 */
    oy2 = wb - bt;      /* 往下推 */

    /* 选最小的那个方向 —— 这就是"从哪里穿进来的" */
    min_ov = ox1; axis = 0;                 /* 0=左 1=右 2=上 3=下 */
    if (ox2 < min_ov) { min_ov = ox2; axis = 1; }
    if (oy1 < min_ov) { min_ov = oy1; axis = 2; }
    if (oy2 < min_ov) { min_ov = oy2; axis = 3; }

    switch (axis)
    {
    case 0:     /* 球在墙左边，往左推 */
        b->x -= min_ov;
        if (b->vx > 0.0f) { b->vx = -b->vx * GAME_BOUNCE; }
        break;
    case 1:     /* 球在墙右边，往右推 */
        b->x += min_ov;
        if (b->vx < 0.0f) { b->vx = -b->vx * GAME_BOUNCE; }
        break;
    case 2:     /* 球在墙上方，往上推 */
        b->y -= min_ov;
        if (b->vy > 0.0f) { b->vy = -b->vy * GAME_BOUNCE; }
        break;
    default:    /* 球在墙下方，往下推 */
        b->y += min_ov;
        if (b->vy < 0.0f) { b->vy = -b->vy * GAME_BOUNCE; }
        break;
    }
}

/* 球和矩形是否有接触（按球的范围算，不是只用球心） */
static uint8_t ball_touches_rect(const Game_Ball *b, const Game_Rect *r)
{
    float bl, br, bt, bb;

    if (r->w <= 0 || r->h <= 0) { return 0; }

    bl = b->x - GAME_BALL_R;  br = b->x + GAME_BALL_R;
    bt = b->y - GAME_BALL_R;  bb = b->y + GAME_BALL_R;

    return (br > (float)r->x) && (bl < (float)(r->x + r->w)) &&
           (bb > (float)r->y) && (bt < (float)(r->y + r->h));
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */

uint8_t Game_PointInRect(float px, float py, const Game_Rect *r)
{
    if (r == 0 || r->w <= 0 || r->h <= 0) { return 0; }
    return (px >= (float)r->x) && (px <= (float)(r->x + r->w)) &&
           (py >= (float)r->y) && (py <= (float)(r->y + r->h));
}

static void load_level(Game_Ctx *g, uint8_t index)
{
    g->level_index = (uint8_t)(index % GAME_LEVEL_COUNT);
    g->level       = &k_levels[g->level_index];

    g->ball.x  = g->level->start_x;
    g->ball.y  = g->level->start_y;
    g->ball.vx = 0.0f;
    g->ball.vy = 0.0f;

    g->elapsed_ms = 0;
    g->hazard_hit = 0;
    g->state      = GAME_RUNNING;
}

void Game_Init(Game_Ctx *g)
{
    if (g == 0) { return; }
    memset(g, 0, sizeof(*g));
    load_level(g, 0);
}

void Game_Reset(Game_Ctx *g)
{
    if (g == 0) { return; }
    load_level(g, g->level_index);
}

void Game_NextLevel(Game_Ctx *g)
{
    if (g == 0) { return; }
    load_level(g, (uint8_t)(g->level_index + 1));
}

const Game_Level *Game_GetLevel(const Game_Ctx *g)
{
    return (g == 0) ? 0 : g->level;
}

void Game_Update(Game_Ctx *g, float tilt_x, float tilt_y, float dt_s)
{
    float ax, ay, tx, ty;
    uint8_t i;

    if (g == 0 || g->state != GAME_RUNNING) { return; }

    g->hazard_hit = 0;
    g->elapsed_ms += (uint32_t)(dt_s * 1000.0f);

    /* ① 死区 */
    tx = tilt_x;
    ty = tilt_y;
    if (tx > -GAME_DEADZONE_LSB && tx < GAME_DEADZONE_LSB) { tx = 0.0f; }
    if (ty > -GAME_DEADZONE_LSB && ty < GAME_DEADZONE_LSB) { ty = 0.0f; }

    /* ② 倾角 → 加速度 */
    ax = GAME_SIGN_X * (tx / LSB_PER_G) * GAME_ACCEL_SCALE;
    ay = GAME_SIGN_Y * (ty / LSB_PER_G) * GAME_ACCEL_SCALE;

    /* ③ 欧拉积分 */
    g->ball.vx = (g->ball.vx + ax * dt_s) * GAME_FRICTION;
    g->ball.vy = (g->ball.vy + ay * dt_s) * GAME_FRICTION;
    clamp_speed(&g->ball);

    g->ball.x += g->ball.vx * dt_s;
    g->ball.y += g->ball.vy * dt_s;

    /* ④ 先处理屏幕边界，再处理墙体（墙体优先，防止被推出去外面） */
    bounce_screen(&g->ball);
    for (i = 0; i < g->level->wall_count; i++)
    {
        resolve_wall(&g->ball, &g->level->walls[i]);
    }
    bounce_screen(&g->ball);

    /* ⑤ 胜负判定：目标区按【球心】进区算赢（不能一出边就赢） */
    if (Game_PointInRect(g->ball.x, g->ball.y, &g->level->target))
    {
        g->state = GAME_WIN;
        return;
    }

    /* 危险区按【球体接触】算，碰一点就死 */
    if (g->level->hazard_on && ball_touches_rect(&g->ball, &g->level->hazard))
    {
        g->hazard_hit = 1;
        g->state      = GAME_FAIL;
    }
}

const char *Game_StateName(Game_State s)
{
    switch (s)
    {
    case GAME_READY:   return "READY  ";
    case GAME_RUNNING: return "RUNNING";
    case GAME_PAUSED:  return "PAUSED ";
    case GAME_WIN:     return "WIN    ";
    case GAME_FAIL:    return "FAIL   ";
    default:           return "?      ";
    }
}
