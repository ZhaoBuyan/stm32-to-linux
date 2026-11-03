/**
 ******************************************************************************
 * @file    app.h
 * @brief   应用层：状态机 + 按钮 + 触摸事件
 *
 * 分层：
 *     触摸硬件  →  Touch_GetTap()  →  App_HandleTap()  →  状态机
 *                                        ↑
 *                                   按钮命中测试
 *
 * 渲染层只要问 App_Buttons() 拿到"当前该画哪些按钮"，画完就行。
 * 它不需要知道按钮是触摸点的、按键触发的、还是串口命令触发的。
 ******************************************************************************
 */
#ifndef __APP_H
#define __APP_H

#include <stdint.h>
#include "game.h"

/* ---------------- 按钮动作 ---------------- */
typedef enum
{
    ACT_NONE = 0,
    ACT_START,          /* 开始游戏 */
    ACT_PAUSE,          /* 暂停 */
    ACT_RESUME,         /* 继续 */
    ACT_RESTART,        /* 重玩本关 */
    ACT_NEXT_LEVEL      /* 进入下一关 */
} App_Action;

/* ---------------- 按钮 ---------------- */
typedef struct
{
    Game_Rect   rect;       /* 屏幕坐标 */
    const char *label;      /* 显示文字 */
    App_Action  action;
} App_Button;

#define APP_MAX_BUTTONS     2

/* ---------------- 应用上下文 ---------------- */
typedef struct
{
    Game_Ctx    game;
    App_Button  buttons[APP_MAX_BUTTONS];
    uint8_t     button_count;
    uint8_t     paused;         /* 冗余标记，方便渲染层判断 */
} App_Ctx;

/* ---------------- 接口 ---------------- */

/** 初始化：建游戏、进入 READY */
void App_Init(App_Ctx *a);

/** 处理一次"点击"（触摸、按键、串口命令都走这里）
 *  @retval 1 = 点中了某个按钮
 */
uint8_t App_HandleTap(App_Ctx *a, int16_t x, int16_t y);

/** 触发某个动作（按键直接调这个，绕过坐标）
 *  @retval 1 = 动作被接受
 */
uint8_t App_DoAction(App_Ctx *a, App_Action action);

/** 每帧调用：推进游戏（只剩 RUNNING 时会动） */
void App_Update(App_Ctx *a, float tilt_x, float tilt_y, float dt_s);

/** 当前状态下该显示哪些按钮（渲染层用） */
const App_Button *App_Buttons(const App_Ctx *a, uint8_t *count);

/** 按当前状态重排按钮（改了状态或改了屏幕尺寸后调用） */
void App_Relayout(App_Ctx *a);

/** 状态名（调试/串口用） */
const char *App_StateName(const App_Ctx *a);

#endif /* __APP_H */
