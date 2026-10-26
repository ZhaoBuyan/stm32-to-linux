/**
 * test_main.c —— EE_WriteBuf 的分页测试（PC 上跑，不需要板子）
 *
 * 覆盖 4 种最容易翻车的形状：
 *   1. 正好一页            （0x00 起写 8 字节）
 *   2. 跨两页对齐          （0x00 起写 16 字节）
 *   3. ★从页中间起跨页      （0x06 起写 4 字节）—— 不分页必错
 *   4. ★页中间起跨三页      （0x23 起写 17 字节）
 */
#include "sim_hal.h"
#include <stdio.h>
#include <string.h>

/* 由 ee_impl.c 提供 */
HAL_StatusTypeDef EE_WriteBuf(uint8_t memAddr, const uint8_t *buf, uint16_t len);

static int g_pass = 0;
static int g_fail = 0;

static void reset_mem(void)
{
    memset(g_ee_mem, 0xFF, sizeof(g_ee_mem));   /* 0xFF = 擦除态，和真芯片新片一样 */
    g_ee_writes   = 0;
    g_ee_delay_ms = 0;
}

/**
 * 跑一个用例：从 addr 写 len 个字节，检查目标区域是否和期望完全一致
 * 期望值 = 0x10 + i（每个字节都不同，错哪一位一眼看出来）
 */
static void run_case(const char *title, uint8_t addr, uint16_t len)
{
    uint8_t expected[256];
    uint8_t pattern[256];
    uint16_t i;
    HAL_StatusTypeDef st;
    int ok = 1;

    reset_mem();

    for (i = 0; i < len; i++)
    {
        pattern[i] = (uint8_t)(0x10 + i);
    }

    /* 期望：先按"正确行为"算出内存应该长什么样 */
    memcpy(expected, g_ee_mem, sizeof(expected));
    for (i = 0; i < len; i++)
    {
        expected[(uint8_t)(addr + i)] = pattern[i];
    }

    st = EE_WriteBuf(addr, pattern, len);

    printf("---- %s ----\n", title);
    printf("  起始 0x%02X，长度 %u，返回 %s\n", addr, len, st == HAL_OK ? "HAL_OK" : "非 HAL_OK");
    printf("  发生页写 %lu 次，累计等待 %lu ms\n",
           (unsigned long)g_ee_writes, (unsigned long)g_ee_delay_ms);

    if (st != HAL_OK)
    {
        printf("  ✗ 返回错误码（还没实现？）\n\n");
        g_fail++;
        return;
    }

    for (i = 0; i < sizeof(expected); i++)
    {
        if (g_ee_mem[i] != expected[i])
        {
            if (ok) printf("  ✗ 内容不符，第一处差异：\n");
            ok = 0;
            printf("      地址 0x%02X：期望 0x%02X，实际 0x%02X\n", i, expected[i], g_ee_mem[i]);
            if (i > 0 && g_ee_mem[i] == pattern[i % len]) {
                printf("      ↑ 这个值是第 %u 个数据字节 —— 典型的页内回绕覆盖\n", (unsigned)(i % len));
            }
            break;   /* 只报第一处，够定位了 */
        }
    }

    /* 附加检查：必须真的等过写周期 */
    if (ok && g_ee_delay_ms == 0)
    {
        ok = 0;
        printf("  ✗ 一次 HAL_Delay 都没调用 —— 写完必须等 tWR(5ms)，否则真片上会丢数据\n");
    }

    if (ok)
    {
        printf("  ✓ 通过\n");
        g_pass++;
    }
    else
    {
        g_fail++;
    }
    printf("\n");
}

int main(void)
{
    printf("======== EE_WriteBuf 分页测试（模拟 AT24C02，页=8 字节）========\n\n");

    run_case("用例 1：正好一页（0x00 起，8 字节）",    0x00, 8);
    run_case("用例 2：对齐跨两页（0x00 起，16 字节）",   0x00, 16);
    run_case("用例 3：★页中间起跨页（0x06 起，4 字节）", 0x06, 4);
    run_case("用例 4：★跨三页（0x23 起，17 字节）",      0x23, 17);
    run_case("用例 5：末尾回绕（0xFE 起，4 字节）",      0xFE, 4);

    printf("======== 结果：%d 通过，%d 失败 ========\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
