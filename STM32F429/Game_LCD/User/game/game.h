/**
 ******************************************************************************
 * @file    game.h
 * @brief   倾斜控制小球 —— 游戏逻辑（与显示完全解耦）
 *
 * 这一层不知道屏幕是什么、怎么画。它只管：
 *     输入倾角 → 更新球的位置 → 碰撞墙体 → 判定胜负
 *
 * 所以现在能先用串口验证，等彩色屏驱动到手，只换渲染层。
 ******************************************************************************
 */
#ifndef __GAME_H
#define __GAME_H

#include <stdint.h>

/* ---------------- 屏幕逻辑尺寸（800x480，屏到了按实际改） ---------------- */
#define GAME_SCREEN_W       800
#define GAME_SCREEN_H       480

/* 顶部留给状态栏：球不许进这一区，
 * 这样状态文字永远不会被球擦掉（渲染层不必每帧重画 HUD） */
#define GAME_PLAY_TOP       50

/* ---------------- 球 ---------------- */
#define GAME_BALL_R         10          /* 半径（像素）。边界和碰撞都要算它 */

/* ---------------- 玩法参数 ---------------- */
#define GAME_ACCEL_SCALE    1500.0f     /* 1g 倾斜给出的加速度（px/s²） */
#define GAME_FRICTION       0.95f       /* 每帧速度衰减 */
#define GAME_BOUNCE         0.80f       /* 碰墙反弹保留比例 */
#define GAME_DEADZONE_LSB   150.0f      /* 死区（约 0.5°） */
#define GAME_MAX_SPEED      900.0f      /* 限速（px/s） */

/* ---------------- 方向标定 ----------------
 * 实测 2026-11-03：抬起板子左边 → tilt_x = -6300（负）→ 球向右（vx 正）
 * 所以 X 要取反。Y 待装屏后确认。
 * -------------------------------------------------------------------------- */
#define GAME_SIGN_X         (-1.0f)
#define GAME_SIGN_Y         (+1.0f)

/* ---------------- 关卡上限 ---------------- */
#define GAME_MAX_WALLS      4
#define GAME_LEVEL_COUNT    12

/* ---------------- 状态 ---------------- */
typedef enum
{
    GAME_READY = 0,
    GAME_RUNNING,
    GAME_PAUSED,
    GAME_WIN,
    GAME_FAIL
} Game_State;

/* ---------------- 球 ---------------- */
typedef struct
{
    float x, y;             /* 球心坐标（像素） */
    float vx, vy;           /* 速度（像素/秒） */
} Game_Ball;

/* ---------------- 矩形（目标区 / 危险区 / 墙体通用） ---------------- */
typedef struct
{
    int16_t x, y, w, h;
} Game_Rect;

/* ---------------- 一关的静态数据 ---------------- */
typedef struct
{
    Game_Rect walls[GAME_MAX_WALLS];
    uint8_t   wall_count;

    Game_Rect hazard;           /* 危险区，hazard_on=0 时无效 */
    uint8_t   hazard_on;

    Game_Rect target;           /* 目标区 */

    float     start_x, start_y; /* 起点（球心） */
} Game_Level;

/* ---------------- 游戏上下文 ---------------- */
typedef struct
{
    Game_Ball  ball;
    Game_State state;

    const Game_Level *level;    /* 当前关卡 */
    uint8_t    level_index;     /* 0 起 */
    uint32_t   elapsed_ms;      /* 本局用时 */

    uint8_t    hazard_hit;      /* 本帧是否碰到危险区（渲染层闪红用） */
} Game_Ctx;

/* ---------------- 接口 ---------------- */

/** 初始化到第 1 关 */
void Game_Init(Game_Ctx *g);

/** 重开当前关卡 */
void Game_Reset(Game_Ctx *g);

/** 进下一关（超出最后一关则回到第 1 关） */
void Game_NextLevel(Game_Ctx *g);

/** 取当前关卡数据（渲染层用） */
const Game_Level *Game_GetLevel(const Game_Ctx *g);

/** 每帧调用
 *  @param tilt_x 扣掉水平参考后的倾斜量（LSB）
 *  @param tilt_y 同上
 *  @param dt_s   距上一帧的秒数
 */
void Game_Update(Game_Ctx *g, float tilt_x, float tilt_y, float dt_s);

/** 状态名（打印用） */
const char *Game_StateName(Game_State s);

/** 矩形工具 */
uint8_t Game_PointInRect(float px, float py, const Game_Rect *r);

#endif /* __GAME_H */
