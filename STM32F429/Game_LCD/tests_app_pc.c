/**
 * app_test.c —— 状态机与按钮的 PC 验证
 */
#include "app.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

static void check(const char *what, int ok)
{
    if (ok) { printf("  OK   %s\n", what); g_pass++; }
    else    { printf("  FAIL %s\n", what); g_fail++; }
}

/* 点某个按钮的正中心 */
static int tap_button(App_Ctx *a, int index)
{
    int cx = a->buttons[index].rect.x + a->buttons[index].rect.w / 2;
    int cy = a->buttons[index].rect.y + a->buttons[index].rect.h / 2;
    return App_HandleTap(a, (int16_t)cx, (int16_t)cy);
}

static void step(App_Ctx *a, float tx, float ty, float dur)
{
    int n = (int)(dur / 0.02f), i;
    for (i = 0; i < n; i++) { App_Update(a, tx, ty, 0.02f); }
}

int main(void)
{
    App_Ctx a;
    uint8_t n;

    printf("===== 应用状态机与按钮验证 =====\n\n");

    printf("用例 1：上电进 READY，只有一个 START 按钮\n");
    App_Init(&a);
    App_Buttons(&a, &n);
    check("状态 = READY", a.game.state == GAME_READY);
    check("按钮数 = 1", n == 1);
    check("按钮动作 = START", a.buttons[0].action == ACT_START);
    printf("\n");

    printf("用例 2：点 START -> RUNNING\n");
    check("点中返回 1", tap_button(&a, 0) == 1);
    check("状态 = RUNNING", a.game.state == GAME_RUNNING);
    App_Buttons(&a, &n);
    check("按钮变成 PAUSE", n == 1 && a.buttons[0].action == ACT_PAUSE);
    printf("\n");

    printf("用例 3：RUNNING 时球会动\n");
    {
        float x0 = a.game.ball.x;
        step(&a, -6300.0f, 0.0f, 1.0f);
        check("球向右移动了", a.game.ball.x > x0 + 20.0f);
    }
    printf("\n");

    printf("用例 4：点 PAUSE -> PAUSED，球定住\n");
    check("点中返回 1", tap_button(&a, 0) == 1);
    check("状态 = PAUSED", a.game.state == GAME_PAUSED);
    {
        float x0 = a.game.ball.x;
        step(&a, -6300.0f, 0.0f, 1.0f);      /* 暂停时继续倾斜 */
        check("暂停后球不动", a.game.ball.x == x0);
    }
    App_Buttons(&a, &n);
    check("两个按钮：RESUME / RESTART",
          n == 2 && a.buttons[0].action == ACT_RESUME &&
                    a.buttons[1].action == ACT_RESTART);
    printf("\n");

    printf("用例 5：点 RESUME -> 继续\n");
    check("点中返回 1", tap_button(&a, 0) == 1);
    check("状态 = RUNNING", a.game.state == GAME_RUNNING);
    {
        float x0 = a.game.ball.x;
        step(&a, -6300.0f, 0.0f, 1.0f);
        check("球又能动了", a.game.ball.x > x0);
    }
    printf("\n");

    printf("用例 6：点 RESTART -> 回起点\n");
    a.game.state = GAME_PAUSED;      /* 先造出 PAUSED 好点到 RESTART */
    App_DoAction(&a, ACT_RESUME);
    App_DoAction(&a, ACT_PAUSE);
    {
        const Game_Level *L = Game_GetLevel(&a.game);
        tap_button(&a, 1);           /* RESTART */
        check("回到本关起点",
              a.game.ball.x == L->start_x && a.game.ball.y == L->start_y);
        check("状态 = RUNNING", a.game.state == GAME_RUNNING);
    }
    printf("\n");

    printf("用例 7：到目标 -> WIN，出现 NEXT / REPLAY\n");
    {
        const Game_Level *L = Game_GetLevel(&a.game);
        a.game.ball.x = (float)(L->target.x + L->target.w / 2);
        a.game.ball.y = (float)(L->target.y + L->target.h / 2);
        App_Update(&a, 0.0f, 0.0f, 0.02f);
        check("状态 = WIN", a.game.state == GAME_WIN);
        App_Buttons(&a, &n);
        check("两个按钮：NEXT / REPLAY",
              n == 2 && a.buttons[0].action == ACT_NEXT_LEVEL &&
                        a.buttons[1].action == ACT_RESTART);
    }
    printf("\n");

    printf("用例 8：点 NEXT -> 进下一关\n");
    {
        uint8_t lv0 = a.game.level_index;
        check("点中返回 1", tap_button(&a, 0) == 1);
        check("关卡 +1", a.game.level_index == (uint8_t)(lv0 + 1));
        check("状态 = RUNNING", a.game.state == GAME_RUNNING);
    }
    printf("\n");

    printf("用例 9：踩红区 -> FAIL，出现 RETRY\n");
    {
        const Game_Level *L = Game_GetLevel(&a.game);
        if (L->hazard_on)
        {
            a.game.ball.x = (float)(L->hazard.x + L->hazard.w / 2);
            a.game.ball.y = (float)(L->hazard.y + L->hazard.h / 2);
            App_Update(&a, 0.0f, 0.0f, 0.02f);
            check("状态 = FAIL", a.game.state == GAME_FAIL);
            App_Buttons(&a, &n);
            check("一个按钮：RETRY", n == 1 && a.buttons[0].action == ACT_RESTART);

            check("点 RETRY 返回 1", tap_button(&a, 0) == 1);
            check("状态 = RUNNING", a.game.state == GAME_RUNNING);
        }
        else
        {
            printf("  （跳过：当前关卡没有红区）\n");
        }
    }
    printf("\n");

    printf("用例 10：点空白处不起作用\n");
    {
        Game_State s0 = a.game.state;
        check("点 (5,5) 返回 0", App_HandleTap(&a, 5, 5) == 0);
        check("状态没变", a.game.state == s0);
    }
    printf("\n");

    printf("用例 11：按钮不重叠、都在屏幕内\n");
    {
        Game_State dummy[] = { GAME_READY, GAME_RUNNING, GAME_PAUSED, GAME_WIN, GAME_FAIL };
        int k, st_ok = 1;

        for (k = 0; k < 5; k++)
        {
            int i, j;
            a.game.state = dummy[k];
            App_Relayout(&a);
            for (i = 0; i < a.button_count; i++)
            {
                Game_Rect *r = &a.buttons[i].rect;
                if (r->x < 0 || r->y < 0 ||
                    r->x + r->w > GAME_SCREEN_W ||
                    r->y + r->h > GAME_SCREEN_H) { st_ok = 0; }
                for (j = i + 1; j < a.button_count; j++)
                {
                    Game_Rect *q = &a.buttons[j].rect;
                    int sep = (r->x + r->w <= q->x) || (q->x + q->w <= r->x) ||
                              (r->y + r->h <= q->y) || (q->y + q->h <= r->y);
                    if (!sep) { st_ok = 0; }
                }
            }
        }
        check("所有状态下按钮都在屏幕内且互不重叠", st_ok);
    }
    printf("\n");

    printf("===== 结果：%d 通过，%d 失败 =====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
