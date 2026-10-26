/**
 * pc_test.c —— 环形缓冲的测试（PC 上跑，不需要板子）
 *
 * 编译运行：
 *   gcc -std=c11 -Wall -Wextra -O2 -o rb_test ringbuf.c pc_test.c
 *   ./rb_test
 *
 * 六组用例，覆盖环形缓冲最容易写错的地方。
 */
#include "ringbuf.h"
#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *what)
{
    if (cond)
    {
        printf("  ✓ %s\n", what);
        g_pass++;
    }
    else
    {
        printf("  ✗ %s\n", what);
        g_fail++;
    }
}

int main(void)
{
    uint8_t  b;
    uint16_t i;
    int      ok;

    printf("======== 环形缓冲测试（RB_SIZE = %d，可用 %d 格）========\n\n", RB_SIZE, RB_SIZE - 1);

    /* ---------------- 用例 1：初始化后是空的 ---------------- */
    printf("用例 1：初始化后必须是空\n");
    RB_Init();
    check(RB_Count() == 0, "Count == 0");

    b = 0xAA;                       /* 预置一个"哨兵值" */
    ok = RB_Get(&b);
    check(ok == 1, "Get 返回 1（空）");
    check(b == 0xAA, "空的时候 *b 不能被改动（失败无副作用）");
    printf("\n");

    /* ---------------- 用例 2：存一个取一个 ---------------- */
    printf("用例 2：存一个、取一个\n");
    RB_Init();
    check(RB_Put(0x12) == 0, "Put 返回 0（成功）");
    check(RB_Count() == 1, "Count == 1");
    b = 0;
    check(RB_Get(&b) == 0, "Get 返回 0（成功）");
    check(b == 0x12, "取出来的值正确");
    check(RB_Count() == 0, "Count 回到 0");
    check(RB_Get(&b) == 1, "再取就是空的");
    printf("\n");

    /* ---------------- 用例 3：装满、溢出 ---------------- */
    printf("用例 3：装满（只能用 %d 格）\n", RB_SIZE - 1);
    RB_Init();
    for (i = 0; i < RB_SIZE - 1; i++)
    {
        if (RB_Put((uint8_t)i) != 0)
        {
            printf("  ✗ 第 %u 个就写不进去了（容量不对）\n", i);
            g_fail++;
            break;
        }
    }
    check(RB_Count() == RB_SIZE - 1, "Count == RB_SIZE-1（刚好存满）");
    check(RB_Put(0xFF) == 1, "再存一个：返回 1（满，丢弃）");
    check(RB_Count() == RB_SIZE - 1, "被丢弃后 Count 不变");
    printf("\n");

    /* ---------------- 用例 4：满了之后取出一个，应该能再存 ---------------- */
    printf("用例 4：满 → 取一个 → 又能存\n");
    b = 0;
    check(RB_Get(&b) == 0, "从满的缓冲里取一个：成功");
    check(b == 0, "取到的是最早进去的那个（0）");
    check(RB_Put(0x99) == 0, "腾出一格后，Put 又能成功");
    check(RB_Count() == RB_SIZE - 1, "Count 又回到 RB_SIZE-1");
    printf("\n");

    /* ---------------- 用例 5：跨过数组末尾（回绕）---------------- */
    printf("用例 5：回绕 —— 塞 50 取 40 再塞 20 取 30\n");
    RB_Init();
    /* 先塞 50 个（0~49） */
    for (i = 0; i < 50; i++)
    {
        if (RB_Put((uint8_t)i) != 0) { printf("  ✗ 塞 50 个失败\n"); g_fail++; break; }
    }
    /* 取 40 个，验证顺序 */
    ok = 1;
    for (i = 0; i < 40; i++)
    {
        if (RB_Get(&b) != 0 || b != (uint8_t)i) { ok = 0; break; }
    }
    check(ok, "前 40 个按 FIFO 顺序取出（0~39）");
    check(RB_Count() == 10, "取出后 Count == 10");

    /* 再塞 20 个（50~69）：head 会跨过数组末尾回绕 */
    ok = 1;
    for (i = 50; i < 70; i++)
    {
        if (RB_Put((uint8_t)i) != 0) { ok = 0; break; }
    }
    check(ok, "再塞 20 个成功（head 跨过末尾回绕）");
    check(RB_Count() == 30, "Count == 30");

    /* 取 30 个，应该是 40~69 */
    ok = 1;
    for (i = 40; i < 70; i++)
    {
        if (RB_Get(&b) != 0 || b != (uint8_t)i) { ok = 0; break; }
    }
    check(ok, "跨回绕后顺序仍然正确（40~69）");
    check(RB_Count() == 0, "最后 Count == 0");
    printf("\n");

    /* ---------------- 用例 6：连续 put/get 三圈，Count 与顺序都对 ---------------- */
    printf("用例 6：连续 3 圈（3 × %d 次），Count 与顺序都对\n", RB_SIZE);
    RB_Init();
    ok = 1;
    for (i = 0; i < 3 * RB_SIZE; i++)
    {
        if (RB_Put((uint8_t)(i * 7)) != 0) { ok = 0; break; }
        if (RB_Count() != 1)               { ok = 0; break; }
        if (RB_Get(&b) != 0)               { ok = 0; break; }
        if (b != (uint8_t)(i * 7))         { ok = 0; break; }
        if (RB_Count() != 0)               { ok = 0; break; }
    }
    check(ok, "3 圈都正确（head/tail 绕了 3 次）");
    printf("\n");

    printf("======== 结果：%d 通过，%d 失败 ========\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
