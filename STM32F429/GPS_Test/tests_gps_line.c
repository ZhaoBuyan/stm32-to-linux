/**
 ******************************************************************************
 * @file    tests_gps_line.c
 * @brief   gps_line.c 的 PC 端单元测试
 *
 * 编译运行（需要 gcc）：
 *   gcc -std=c11 -Wall -Wextra -IUser/gps tests_gps_line.c User/gps/gps_line.c -o tests_gps_line.exe
 *   ./tests_gps_line.exe
 *
 * 不用板子、不用串口，只验证"字节流 -> 完整行"这段逻辑。
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "gps_line.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s\n", name); }
}

/* 把一串字节喂进去，返回组出了几行；lines 用来收结果 */
static int feed(Gps_LineAsm *la, const char *s, char lines[][GPS_LINE_MAX], int max)
{
    int n = 0;
    while (*s)
    {
        int r = Gps_LineFeed(la, (unsigned char)*s++);
        if (r == 1 && n < max)
        {
            strcpy(lines[n], Gps_LineText(la));
            n++;
        }
    }
    return n;
}

int main(void)
{
    Gps_LineAsm la;
    char lines[8][GPS_LINE_MAX];

    printf("== gps_line tests ==\n");

    /* 1. 一条完整语句 + CRLF，应只组出 1 行（空行被吃掉） */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "$GNRMC,1,2,3*5E\r\n", lines, 8);
        check(n == 1, "CRLF produces exactly one line");
        check(strcmp(lines[0], "$GNRMC,1,2,3*5E") == 0, "content without CRLF");
    }

    /* 2. 只用 \n 结尾 */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "$GPGGA,abc\n", lines, 8);
        check(n == 1, "LF only produces one line");
        check(strcmp(lines[0], "$GPGGA,abc") == 0, "LF content correct");
    }

    /* 3. 只用 \r 结尾（少数模块这样） */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "$GPGGA,cr\r", lines, 8);
        check(n == 1, "CR only produces one line");
        check(strcmp(lines[0], "$GPGGA,cr") == 0, "CR content correct");
    }

    /* 4. 连续三行 */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "$A\r\n$B\r\n$C\r\n", lines, 8);
        check(n == 3, "three lines back to back");
        check(strcmp(lines[0], "$A") == 0 && strcmp(lines[1], "$B") == 0 &&
              strcmp(lines[2], "$C") == 0, "three lines content order");
    }

    /* 5. 半行（没有行尾），不应产出 */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "$GNRMC,incomplete", lines, 8);
        check(n == 0, "incomplete line yields nothing");
        check(strcmp(Gps_LineText(&la), "$GNRMC,incomplete") == 0,
              "incomplete content still buffered");
    }

    /* 6. 拼半行：先来一半，再来另一半 + 行尾 */
    memset(&la, 0, sizeof(la));
    {
        int n1 = feed(&la, "$GNRM", lines, 8);
        int n2 = feed(&la, "C,rest*7F\r\n", lines, 8);
        check(n1 == 0 && n2 == 1, "split line reassembled");
        check(strcmp(lines[0], "$GNRMC,rest*7F") == 0, "split line content");
    }

    /* 7. 空行开头（\r\n\r\n）不应产出任何东西 */
    memset(&la, 0, sizeof(la));
    {
        int n = feed(&la, "\r\n\r\n", lines, 8);
        check(n == 0, "empty lines ignored");
    }

    /* 8. 超长行应被丢弃（返回 -1，产出 0 行），且 overflow 计数为 1 */
    memset(&la, 0, sizeof(la));
    {
        char big[GPS_LINE_MAX + 20];
        int i, n, saw_minus1 = 0;
        memset(big, 'X', sizeof(big));
        big[sizeof(big) - 1] = '\0';

        n = 0;
        for (i = 0; big[i]; i++)
        {
            int r = Gps_LineFeed(&la, (unsigned char)big[i]);
            if (r == -1) { saw_minus1 = 1; }
            else if (r == 1) { n++; }
        }
        check(saw_minus1 == 1, "overlong line returns -1");
        check(n == 0, "overlong line produces no output");
        check(la.overflow == 1, "overflow counter increments");
    }

    /* 9. 超长行（自带行尾）之后能正常恢复：新的合法行仍然能组出来，
     *    而且新行开头必须是 '$'，不能被超长行的尾巴污染。 */
    memset(&la, 0, sizeof(la));
    {
        char big[GPS_LINE_MAX + 10];
        int i, n;
        memset(big, 'Y', sizeof(big));
        big[sizeof(big) - 1] = '\0';

        for (i = 0; big[i]; i++) { (void)Gps_LineFeed(&la, (unsigned char)big[i]); }
        n = feed(&la, "\r\n$GPGGA,after\r\n", lines, 8);
        check(n == 1, "recovers after overlong line");
        check(strcmp(lines[0], "$GPGGA,after") == 0, "recovered content clean");
    }

    /* 9b. 超长行的尾巴不能粘到下一行开头 */
    memset(&la, 0, sizeof(la));
    {
        int i, n;
        for (i = 0; i < GPS_LINE_MAX + 5; i++) { (void)Gps_LineFeed(&la, 'T'); }
        n = feed(&la, "TAIL\r\n$GPGGA,ok\r\n", lines, 8);
        check(n == 1, "tail of overlong line does not leak");
        check(strcmp(lines[0], "$GPGGA,ok") == 0, "tail discarded, next line clean");
    }

    /* 10. 恰好放满（GPS_LINE_MAX-1 个字符）应当成功，不算超长 */
    memset(&la, 0, sizeof(la));
    {
        int i, n;
        for (i = 0; i < GPS_LINE_MAX - 1; i++) { (void)Gps_LineFeed(&la, 'A'); }
        n = Gps_LineFeed(&la, '\n');
        check(n == 1, "exactly max-1 chars fits");
        check(strlen(Gps_LineText(&la)) == (size_t)(GPS_LINE_MAX - 1),
              "buffered length is max-1");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
