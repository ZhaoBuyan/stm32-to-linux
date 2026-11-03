/**
 * game_test2.c —— PC 上验证游戏物理、碰撞、关卡可达性
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;

static void check(const char *what, int ok)
{
    if (ok) { printf("  OK   %s\n", what); g_pass++; }
    else    { printf("  FAIL %s\n", what); g_fail++; }
}

static void run(Game_Ctx *g, float tx, float ty, float dur_s)
{
    int frames = (int)(dur_s / 0.02f), i;
    for (i = 0; i < frames; i++) { Game_Update(g, tx, ty, 0.02f); }
}

/* 球（按 2R 方块）与矩形是否重叠 */
static int overlaps(const Game_Ball *b, const Game_Rect *r)
{
    if (r->w <= 0 || r->h <= 0) return 0;
    return (b->x + GAME_BALL_R > r->x) && (b->x - GAME_BALL_R < r->x + r->w) &&
           (b->y + GAME_BALL_R > r->y) && (b->y - GAME_BALL_R < r->y + r->h);
}

/* ============================================================================
 * 网格 BFS：判断关卡有没有通路
 * ========================================================================== */
#define STEP   8
#define GW     (GAME_SCREEN_W / STEP)
#define GH     (GAME_SCREEN_H / STEP)

static unsigned char visited[GW][GH];
static int qx[GW * GH], qy[GW * GH];

static int cell_free(const Game_Level *L, float x, float y)
{
    Game_Ball b;
    int i;

    if (x < GAME_BALL_R || x > GAME_SCREEN_W - GAME_BALL_R) return 0;
    if (y < GAME_BALL_R || y > GAME_SCREEN_H - GAME_BALL_R) return 0;

    b.x = x; b.y = y; b.vx = b.vy = 0.0f;

    for (i = 0; i < L->wall_count; i++)
    {
        if (overlaps(&b, &L->walls[i])) return 0;
    }
    if (L->hazard_on && overlaps(&b, &L->hazard)) return 0;

    return 1;
}

static int level_reachable(const Game_Level *L, float sx, float sy)
{
    int head = 0, tail = 0;
    int cx = (int)(sx / STEP);
    int cy = (int)(sy / STEP);
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int i;

    memset(visited, 0, sizeof(visited));

    if (cx < 0 || cx >= GW || cy < 0 || cy >= GH) return 0;

    qx[tail] = cx; qy[tail] = cy; tail++;
    visited[cx][cy] = 1;

    while (head < tail)
    {
        int x = qx[head], y = qy[head];
        float wx = (float)x * STEP + (float)STEP / 2.0f;
        float wy = (float)y * STEP + (float)STEP / 2.0f;

        head++;

        if (Game_PointInRect(wx, wy, &L->target)) return 1;

        for (i = 0; i < 4; i++)
        {
            int nx = x + dx[i], ny = y + dy[i];
            float px, py;

            if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;
            if (visited[nx][ny]) continue;

            px = (float)nx * STEP + (float)STEP / 2.0f;
            py = (float)ny * STEP + (float)STEP / 2.0f;
            if (!cell_free(L, px, py)) continue;

            visited[nx][ny] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    return 0;
}

int main(void)
{
    Game_Ctx g;
    int lv;

    printf("===== 游戏验证（%dx%d，球半径 %d，%d 关）=====\n\n",
           GAME_SCREEN_W, GAME_SCREEN_H, GAME_BALL_R, GAME_LEVEL_COUNT);

    printf("用例 1：放平不动\n");
    Game_Init(&g);
    {
        float x0 = g.ball.x, y0 = g.ball.y;
        run(&g, 0.0f, 0.0f, 3.0f);
        check("x 不动", fabsf(g.ball.x - x0) < 0.5f);
        check("y 不动", fabsf(g.ball.y - y0) < 0.5f);
    }
    printf("\n");

    printf("用例 2：死区挡住微抖\n");
    Game_Init(&g);
    {
        float x0 = g.ball.x;
        run(&g, 120.0f, -120.0f, 2.0f);
        check("微抖 2 秒几乎没动", fabsf(g.ball.x - x0) < 0.5f);
    }
    printf("\n");

    printf("用例 3：抬左边 -> 球向右\n");
    Game_Init(&g);
    {
        float x0 = g.ball.x;
        run(&g, -6300.0f, 0.0f, 2.0f);
        check("x 增大", g.ball.x > x0 + 40.0f);
        check("vx > 0", g.ball.vx > 0.0f);
    }
    printf("\n");

    printf("用例 4：抬右边 -> 球向左\n");
    Game_Reset(&g);
    {
        float x0 = g.ball.x;
        run(&g, 6300.0f, 0.0f, 2.0f);
        check("x 减小", g.ball.x < x0 - 40.0f);
    }
    printf("\n");

    printf("用例 5：一直往右倾，不能穿出右墙\n");
    Game_Reset(&g);
    run(&g, -6300.0f, 0.0f, 20.0f);
    check("x <= W-R", g.ball.x <= (float)(GAME_SCREEN_W - GAME_BALL_R) + 0.01f);
    printf("       (x = %.1f, 上限 %d)\n", g.ball.x, GAME_SCREEN_W - GAME_BALL_R);
    printf("\n");

    printf("用例 6：撞竖墙不能穿过去\n");
    Game_Init(&g);
    Game_NextLevel(&g);
    check("已在第 2 关", g.level_index == 1);
    {
        const Game_Level *L = Game_GetLevel(&g);
        const Game_Rect  *w = &L->walls[0];
        int i, ever_inside = 0;

        g.ball.x = (float)(w->x - 60);
        g.ball.y = (float)(w->y + 60);

        for (i = 0; i < 300; i++)
        {
            Game_Update(&g, -6300.0f, 0.0f, 0.02f);
            if (overlaps(&g.ball, w)) { ever_inside = 1; }
        }
        check("全程没有进到墙内部", ever_inside == 0);
        check("被墙挡在左边", g.ball.x + GAME_BALL_R <= (float)w->x + 0.5f);
        printf("       (x = %.1f, 墙左边 = %d)\n", g.ball.x, w->x);
    }
    printf("\n");

    printf("用例 7：沿墙滑动，最终绕过墙底\n");
    {
        const Game_Level *L = Game_GetLevel(&g);
        float y0 = g.ball.y;
        float wall_bottom = (float)(L->walls[0].y + L->walls[0].h);

        run(&g, -6300.0f, -6300.0f, 3.0f);

        check("y 明显增大（沿墙下滑）", g.ball.y > y0 + 100.0f);
        check("已经滑到墙底以下（绕过墙了）", g.ball.y > wall_bottom + GAME_BALL_R);
        printf("       (y: %.1f -> %.1f, 墙底 = %.1f)\n", y0, g.ball.y, wall_bottom);
    }
    printf("\n");

    printf("用例 8：进目标区判 WIN\n");
    Game_Init(&g);
    {
        const Game_Level *L = Game_GetLevel(&g);
        g.ball.x = (float)(L->target.x + L->target.w / 2);
        g.ball.y = (float)(L->target.y + L->target.h / 2);
        Game_Update(&g, 0.0f, 0.0f, 0.02f);
        check("状态 = WIN", g.state == GAME_WIN);
    }
    printf("\n");

    printf("用例 9：碰危险区判 FAIL\n");
    Game_Init(&g);
    Game_NextLevel(&g);
    {
        const Game_Level *L = Game_GetLevel(&g);
        check("第 2 关有危险区", L->hazard_on == 1);

        g.ball.x = (float)(L->hazard.x + L->hazard.w / 2);
        g.ball.y = (float)(L->hazard.y + L->hazard.h / 2);
        Game_Update(&g, 0.0f, 0.0f, 0.02f);
        check("球心进红区 = FAIL", g.state == GAME_FAIL);
    }
    printf("\n");

    printf("用例 10：关卡切换与回绕\n");
    Game_Init(&g);
    check("初始第 1 关", g.level_index == 0);
    Game_NextLevel(&g);
    check("下一关 = 2", g.level_index == 1);
    Game_NextLevel(&g);
    check("下一关 = 3", g.level_index == 2);
    Game_NextLevel(&g);
    check("再下一关回绕到 1", g.level_index == 0);
    printf("\n");

    printf("用例 11：Reset 回到起点\n");
    Game_Init(&g);
    Game_NextLevel(&g);
    run(&g, -6300.0f, 0.0f, 1.0f);
    Game_Reset(&g);
    {
        const Game_Level *L = Game_GetLevel(&g);
        check("位置回到起点", g.ball.x == L->start_x && g.ball.y == L->start_y);
        check("速度清零", g.ball.vx == 0.0f && g.ball.vy == 0.0f);
        check("状态变回 RUNNING", g.state == GAME_RUNNING);
    }
    printf("\n");

    printf("用例 12：每关几何合法性\n");
    for (lv = 0; lv < GAME_LEVEL_COUNT; lv++)
    {
        const Game_Level *L;
        char tag[96];
        int k, ok_pos, ok_target, ok_free, ok_not_in;

        Game_Init(&g);
        for (k = 0; k < lv; k++) { Game_NextLevel(&g); }
        L = Game_GetLevel(&g);

        ok_pos = (g.ball.x >= GAME_BALL_R) &&
                 (g.ball.x <= GAME_SCREEN_W - GAME_BALL_R) &&
                 (g.ball.y >= GAME_BALL_R) &&
                 (g.ball.y <= GAME_SCREEN_H - GAME_BALL_R);

        ok_target = (L->target.x >= 0) && (L->target.y >= 0) &&
                    (L->target.x + L->target.w <= GAME_SCREEN_W) &&
                    (L->target.y + L->target.h <= GAME_SCREEN_H);

        ok_free = 1;
        for (k = 0; k < L->wall_count; k++)
        {
            if (overlaps(&g.ball, &L->walls[k])) { ok_free = 0; }
        }

        ok_not_in = !Game_PointInRect(g.ball.x, g.ball.y, &L->target);
        if (L->hazard_on && overlaps(&g.ball, &L->hazard)) { ok_not_in = 0; }

        printf("    第 %d 关: 墙 %d 道, 危区 %s, 起点(%.0f,%.0f)\n",
               lv + 1, L->wall_count, L->hazard_on ? "有" : "无",
               g.ball.x, g.ball.y);

        sprintf(tag, "第 %d 关 起点在屏幕内", lv + 1);        check(tag, ok_pos);
        sprintf(tag, "第 %d 关 目标区在屏幕内", lv + 1);      check(tag, ok_target);
        sprintf(tag, "第 %d 关 起点没卡在墙里", lv + 1);      check(tag, ok_free);
        sprintf(tag, "第 %d 关 起点不在目标/红区里", lv + 1); check(tag, ok_not_in);
    }
    printf("\n");

    printf("用例 13：每关都存在通往目标的通路（网格 BFS）\n");
    for (lv = 0; lv < GAME_LEVEL_COUNT; lv++)
    {
        const Game_Level *L;
        char tag[96];
        int k, ok;

        Game_Init(&g);
        for (k = 0; k < lv; k++) { Game_NextLevel(&g); }
        L = Game_GetLevel(&g);

        ok = level_reachable(L, g.ball.x, g.ball.y);
        printf("    第 %d 关: %s\n", lv + 1, ok ? "通路存在" : "被封死");

        sprintf(tag, "第 %d 关 存在可达通路", lv + 1);
        check(tag, ok);
    }
    printf("\n");

    printf("===== 结果：%d 通过，%d 失败 =====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
