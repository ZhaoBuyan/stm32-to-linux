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

    printf("用例 6：任何关卡、任何方向推，球都不会进到墙内部\n");
    {
        int lv, k, ever_inside = 0;

        for (lv = 0; lv < GAME_LEVEL_COUNT; lv++)
        {
            const Game_Level *L;
            int wi;

            Game_Init(&g);
            for (k = 0; k < lv; k++) { Game_NextLevel(&g); }
            L = Game_GetLevel(&g);

            /* 四个方向各推 2 秒，全程检查有没有和墙重叠 */
            for (k = 0; k < 400; k++)
            {
                float tx = 0.0f, ty = 0.0f;
                if      (k < 100) { tx = -6300.0f; }
                else if (k < 200) { tx =  6300.0f; }
                else if (k < 300) { ty =  6300.0f; }
                else              { ty = -6300.0f; }

                Game_Update(&g, tx, ty, 0.02f);
                for (wi = 0; wi < L->wall_count; wi++)
                {
                    if (overlaps(&g.ball, &L->walls[wi])) { ever_inside = 1; }
                }
            }
        }
        check("所有关卡×四个方向：全程没有穿墙", ever_inside == 0);
    }
    printf("\n");

    printf("用例 7：贴墙时还能沿墙滑动（不卡死）\n");
    {
        /* 第 5 关有两道错开的竖墙，贴着第一道能滑到下面绕过去 */
        int k;
        float y0;
        const Game_Level *L;

        Game_Init(&g);
        for (k = 0; k < 4; k++) { Game_NextLevel(&g); }     /* 进第 5 关 */
        L = Game_GetLevel(&g);
        check("第 5 关有两道墙", L->wall_count == 2);

        g.ball.x = (float)(L->walls[0].x - 40);
        g.ball.y = (float)(L->walls[0].y + 80);
        y0 = g.ball.y;

        run(&g, -6300.0f, 6300.0f, 3.0f);       /* 一边右推一边下推 */
        check("y 有明显变化（沿墙滑动了）", fabsf(g.ball.y - y0) > 100.0f);
        printf("       (y: %.1f -> %.1f)\n", y0, g.ball.y);
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
    {
        int lv, found = -1;

        /* 找到第一个带红区的关卡（不再写死是第几关） */
        for (lv = 0; lv < GAME_LEVEL_COUNT; lv++)
        {
            int k;
            Game_Init(&g);
            for (k = 0; k < lv; k++) { Game_NextLevel(&g); }
            if (Game_GetLevel(&g)->hazard_on) { found = lv; break; }
        }

        if (found < 0)
        {
            printf("  （跳过：没有任何关卡带红区）\n");
        }
        else
        {
            const Game_Level *L;
            int k;

            Game_Init(&g);
            for (k = 0; k < found; k++) { Game_NextLevel(&g); }
            L = Game_GetLevel(&g);

            printf("  （用第 %d 关测）\n", found + 1);
            check("这一关有危险区", L->hazard_on == 1);

            g.ball.x = (float)(L->hazard.x + L->hazard.w / 2);
            g.ball.y = (float)(L->hazard.y + L->hazard.h / 2);
            Game_Update(&g, 0.0f, 0.0f, 0.02f);
            check("球心进红区 = FAIL", g.state == GAME_FAIL);
        }
    }
    printf("\n");

    printf("用例 10：关卡切换与回绕\n");
    Game_Init(&g);
    {
        int k;
        check("初始第 1 关", g.level_index == 0);
        for (k = 1; k < GAME_LEVEL_COUNT; k++)
        {
            char tag[64];
            Game_NextLevel(&g);
            sprintf(tag, "第 %d 次 NextLevel -> 关卡 %d", k, k + 1);
            check(tag, g.level_index == (uint8_t)k);
        }
        Game_NextLevel(&g);
        check("最后一关再下一关，回绕到第 1 关", g.level_index == 0);
    }
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
