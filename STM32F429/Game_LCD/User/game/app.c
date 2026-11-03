/**
 ******************************************************************************
 * @file    app.c
 * @brief   状态机 + 按钮布局 + 点击命中
 *
 * 状态流转：
 *   READY  --开始-->  RUNNING --暂停--> PAUSED --继续--> RUNNING
 *                        |                              |
 *                        +--到目标--> WIN --下一关--> RUNNING
 *                        +--踩红区--> FAIL --重试--> RUNNING
 ******************************************************************************
 */
#include "app.h"
#include <string.h>

/* 按钮统一尺寸 */
#define BTN_W        200
#define BTN_H         64
#define BTN_GAP       30

/* 屏幕下沿留出的按钮区高度 */
#define BTN_BOTTOM_Y (GAME_SCREEN_H - BTN_H - 40)

static void set_button(App_Button *b, int16_t x, int16_t y,
                       const char *label, App_Action act)
{
    b->rect.x = x;
    b->rect.y = y;
    b->rect.w = BTN_W;
    b->rect.h = BTN_H;
    b->label  = label;
    b->action = act;
}

/* 自定义尺寸的按钮（状态栏里那个小的暂停按钮用） */
static void set_button_ex(App_Button *b, int16_t x, int16_t y,
                          int16_t w, int16_t h,
                          const char *label, App_Action act)
{
    b->rect.x = x;
    b->rect.y = y;
    b->rect.w = w;
    b->rect.h = h;
    b->label  = label;
    b->action = act;
}

/* 按当前状态布置按钮 */
static void layout_buttons(App_Ctx *a)
{
    a->button_count = 0;

    switch (a->game.state)
    {
    case GAME_READY:
        /* 居中一个「开始游戏」 */
        set_button(&a->buttons[0],
                   (int16_t)((GAME_SCREEN_W - BTN_W) / 2), BTN_BOTTOM_Y,
                   "START", ACT_START);
        a->button_count = 1;
        break;

    case GAME_RUNNING:
        /* 暂停按钮放进【顶部状态栏内部】，不占游戏区
         * 位置：状态栏右侧，高 30px，居中在 50px 高的状态栏里 */
        set_button_ex(&a->buttons[0],
                      (int16_t)(GAME_SCREEN_W - 104), 10,
                      92, 30,
                      "PAUSE", ACT_PAUSE);
        a->button_count = 1;
        break;

    case GAME_PAUSED:
        /* 并排两个：继续 / 重来 */
        set_button(&a->buttons[0],
                   (int16_t)((GAME_SCREEN_W / 2) - BTN_W - BTN_GAP / 2), BTN_BOTTOM_Y,
                   "RESUME", ACT_RESUME);
        set_button(&a->buttons[1],
                   (int16_t)((GAME_SCREEN_W / 2) + BTN_GAP / 2), BTN_BOTTOM_Y,
                   "RESTART", ACT_RESTART);
        a->button_count = 2;
        break;

    case GAME_WIN:
        set_button(&a->buttons[0],
                   (int16_t)((GAME_SCREEN_W / 2) - BTN_W - BTN_GAP / 2), BTN_BOTTOM_Y,
                   "NEXT", ACT_NEXT_LEVEL);
        set_button(&a->buttons[1],
                   (int16_t)((GAME_SCREEN_W / 2) + BTN_GAP / 2), BTN_BOTTOM_Y,
                   "REPLAY", ACT_RESTART);
        a->button_count = 2;
        break;

    case GAME_FAIL:
        set_button(&a->buttons[0],
                   (int16_t)((GAME_SCREEN_W - BTN_W) / 2), BTN_BOTTOM_Y,
                   "RETRY", ACT_RESTART);
        a->button_count = 1;
        break;

    default:
        a->button_count = 0;
        break;
    }
}

void App_Init(App_Ctx *a)
{
    if (a == 0) { return; }

    memset(a, 0, sizeof(*a));

    Game_Init(&a->game);
    a->game.state = GAME_READY;         /* 上电先停在 READY，等玩家点开始 */
    a->paused = 0;

    layout_buttons(a);
}

uint8_t App_HandleTap(App_Ctx *a, int16_t x, int16_t y)
{
    uint8_t i;

    if (a == 0) { return 0; }

    for (i = 0; i < a->button_count; i++)
    {
        if (Game_PointInRect((float)x, (float)y, &a->buttons[i].rect))
        {
            return App_DoAction(a, a->buttons[i].action);
        }
    }
    return 0;
}

uint8_t App_DoAction(App_Ctx *a, App_Action action)
{
    if (a == 0) { return 0; }

    switch (action)
    {
    case ACT_START:
        if (a->game.state == GAME_READY)
        {
            a->game.state = GAME_RUNNING;
        }
        else { return 0; }
        break;

    case ACT_PAUSE:
        if (a->game.state == GAME_RUNNING)
        {
            a->game.state = GAME_PAUSED;
        }
        else { return 0; }
        break;

    case ACT_RESUME:
        if (a->game.state == GAME_PAUSED)
        {
            a->game.state = GAME_RUNNING;
        }
        else { return 0; }
        break;

    case ACT_RESTART:
        /* 任何状态都能重来 */
        Game_Reset(&a->game);
        a->game.state = GAME_RUNNING;
        break;

    case ACT_NEXT_LEVEL:
        if (a->game.state == GAME_WIN)
        {
            Game_NextLevel(&a->game);
            a->game.state = GAME_RUNNING;
        }
        else { return 0; }
        break;

    default:
        return 0;
    }

    a->paused = (uint8_t)(a->game.state == GAME_PAUSED);
    layout_buttons(a);
    return 1;
}

void App_Update(App_Ctx *a, float tilt_x, float tilt_y, float dt_s)
{
    Game_State before;

    if (a == 0) { return; }

    before = a->game.state;
    Game_Update(&a->game, tilt_x, tilt_y, dt_s);

    /* 状态被游戏自己改了（WIN / FAIL），按钮要重排 */
    if (a->game.state != before)
    {
        a->paused = (uint8_t)(a->game.state == GAME_PAUSED);
        layout_buttons(a);
    }
}

const App_Button *App_Buttons(const App_Ctx *a, uint8_t *count)
{
    if (a == 0 || count == 0) { return 0; }
    *count = a->button_count;
    return a->buttons;
}

void App_Relayout(App_Ctx *a)
{
    if (a == 0) { return; }
    layout_buttons(a);
}

const char *App_StateName(const App_Ctx *a)
{
    if (a == 0) { return "?"; }
    return Game_StateName(a->game.state);
}
