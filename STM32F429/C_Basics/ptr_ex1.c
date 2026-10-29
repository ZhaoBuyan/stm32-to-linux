/**
 * ptr_ex1.c —— 指针练习：原地反转
 *
 * 任务：把 reverse_buf 写出来。
 *
 * 要求：
 *   - 只准用指针（*p、*q、p++、q--），不要用 buf[i]
 *   - 原地反转，不能申请新数组
 *   - 奇数个和偶数个元素都要对
 *
 * 编译运行：
 *   gcc -std=c11 -Wall -Wextra -o ptr_ex1.exe ptr_ex1.c
 *   .\ptr_ex1.exe
 */
#include <stdio.h>
#include <stdint.h>

/* ============================================================================
 * ★ 你要写的
 *
 * 思路：
 *   1. 两个指针：left 指向第一个字节，right 指向最后一个字节
 *      （最后一个 = buf + len - 1）
 *   2. 只要 left 还在 right 左边：
 *        a. 交换 *left 和 *right 指向的内容
 *        b. left 往后挪，right 往前挪
 *   3. len 是 0 或 1 时不该有任何动作（循环条件天然处理了，但要确认）
 *
 * 想一想：循环条件写 while (left < right) 还是 while (left <= right)？
 *         len 是偶数 / 奇数时分别走几次？
 * ========================================================================== */
void reverse_buf(uint8_t *buf, uint16_t len)
{
    /* TODO */
    uint8_t *left = buf;
    uint8_t *right = buf + len - 1;
    uint8_t t;

        while (left < right) {
        t = *left;
        *left = *right;
        *right = t;
        left++;
        right--;
    }
}

/* ============================================================================
 * 下面是测试，不用改
 * ========================================================================== */
static int g_pass = 0, g_fail = 0;

static void show(const char *tag, const uint8_t *b, uint16_t n)
{
    uint16_t i;
    printf("  %s = { ", tag);
    for (i = 0; i < n; i++) printf("%u ", (unsigned)b[i]);
    printf("}\n");
}

static void check(const char *what, int ok)
{
    if (ok) { printf("  ✓ %s\n", what); g_pass++; }
    else    { printf("  ✗ %s\n", what); g_fail++; }
}

int main(void)
{
    /* 用例 1：奇数个 */
    {
        uint8_t a[5] = { 1, 2, 3, 4, 5 };
        uint8_t want[5] = { 5, 4, 3, 2, 1 };
        int ok = 1, i;
        printf("用例 1：奇数个 %d 个\n", 5);
        reverse_buf(a, 5);
        show("结果", a, 5);
        for (i = 0; i < 5; i++) if (a[i] != want[i]) ok = 0;
        check("{1,2,3,4,5} → {5,4,3,2,1}", ok);
        printf("\n");
    }

    /* 用例 2：偶数个 */
    {
        uint8_t a[4] = { 1, 2, 3, 4 };
        uint8_t want[4] = { 4, 3, 2, 1 };
        int ok = 1, i;
        printf("用例 2：偶数个 %d 个\n", 4);
        reverse_buf(a, 4);
        show("结果", a, 4);
        for (i = 0; i < 4; i++) if (a[i] != want[i]) ok = 0;
        check("{1,2,3,4} → {4,3,2,1}", ok);
        printf("\n");
    }

    /* 用例 3：边界 —— len = 1 和 0 */
    {
        uint8_t a[1] = { 42 };
        printf("用例 3：边界\n");
        reverse_buf(a, 1);
        check("len=1 时不变", a[0] == 42);
        reverse_buf(a, 0);
        check("len=0 不崩、不变", a[0] == 42);
        printf("\n");
    }

    /* 用例 4：真实场景 —— 反转一个字符串 */
    {
        char s[] = "hello";
        printf("用例 4：反转字符串\n");
        reverse_buf((uint8_t *)s, 5);
        printf("  \"hello\" → \"%s\"\n", s);
        check("hello → olleh", s[0] == 'o' && s[4] == 'h');
        printf("\n");
    }

    printf("======== 结果：%d 通过，%d 失败 ========\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
