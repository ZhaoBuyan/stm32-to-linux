/**
 ******************************************************************************
 * @file    eeprom.c
 * @brief   AT24C02 驱动实现（硬件 I2C + HAL）
 *
 * 提供：单字节读 / 单字节写 / 多字节连续读写（按 8 字节分页）/ 总线扫描 / 回读自检
 ******************************************************************************
 */
#include "eeprom.h"
#include <stdio.h>
#include <string.h>

/* 这两个句柄由 CubeMX 在 main.c 里定义 */
extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart1;

/* ---------------------------- 小工具 ---------------------------- */
static void dbg(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/**
 * @brief  写 1 个字节
 * @note   HAL_I2C_Mem_Write 的第 2 个参数是 **8 位形式**的地址（0x50 << 1 = 0xA0）；
 *         写完必须等 5 ms 写周期，否则芯片还在内部烧写，下一条命令会被丢弃。
 */
HAL_StatusTypeDef EE_WriteByte(uint8_t memAddr, uint8_t data)
{
    HAL_StatusTypeDef st;

    st = HAL_I2C_Mem_Write(&hi2c1,
                           AT24C02_ADDR,              /* 0xA0 */
                           memAddr,                   /* 字地址 0x00~0xFF */
                           I2C_MEMADD_SIZE_8BIT,      /* AT24C02 的字地址是 1 字节 */
                           &data, 1, 100);
    HAL_Delay(AT24C02_TWR_MS);
    return st;
}

/**
 * @brief  读 1 个字节
 */
HAL_StatusTypeDef EE_ReadByte(uint8_t memAddr, uint8_t *data)
{
    return HAL_I2C_Mem_Read(&hi2c1,
                            AT24C02_ADDR,
                            memAddr,
                            I2C_MEMADD_SIZE_8BIT,
                            data, 1, 100);
}

/**
 * @brief  写一块数据（按 8 字节分页）
 *
 * 为什么必须分页：AT24C02 的页写指针**在页内回绕**。
 * 从 0x06 开始一次写 4 个字节 → 实际落到 0x06, 0x07, 0x00, 0x01
 * 后面两个把页开头的 0x00, 0x01 覆盖了，**而且芯片不报任何错**。
 *
 * 做法：每次只写「当前页还剩下的空位」那么多，写完等 tWR，再推进。
 *   room = 8 - (地址 % 8)        → 本页还剩几格
 *   n    = min(剩余长度, room)   → 本次最多写几个
 *
 * 已在 PC 模拟器上验证 5 种形状（单页 / 跨两页 / 页中间起 / 跨三页 / 末尾回绕）全部通过。
 */
HAL_StatusTypeDef EE_WriteBuf(uint8_t memAddr, const uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;
    uint16_t room;
    uint16_t n;

    while (len > 0)
    {
        room = (uint16_t)(AT24C02_PAGE - (memAddr % AT24C02_PAGE));
        n    = (len < room) ? len : room;

        st = HAL_I2C_Mem_Write(&hi2c1, AT24C02_ADDR, memAddr,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, n, 100);
        if (st != HAL_OK)
        {
            return st;
        }

        HAL_Delay(AT24C02_TWR_MS);        /* 每写完一页都要等 tWR */

        memAddr = (uint8_t)(memAddr + n); /* 0xFF 之后自动回绕到 0x00 */
        buf    += n;
        len    -= n;
    }

    return HAL_OK;
}

/**
 * @brief  读一块数据
 * @note   读不需要分页：AT24C02 的连续读会自动递增地址并回绕，
 *         HAL_I2C_Mem_Read 一次读完即可。这和"写要分页"的不对称性值得记住。
 */
HAL_StatusTypeDef EE_ReadBuf(uint8_t memAddr, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1,
                            AT24C02_ADDR,
                            memAddr,
                            I2C_MEMADD_SIZE_8BIT,
                            buf, len, 100);
}

/**
 * @brief  扫描 I2C1 总线，打印所有应答的设备地址
 *
 * 用途：
 *   1. 确认 AT24C02（应看到 0x50）挂在哪条总线上；
 *   2. 确认 OLED（应看到 0x3C）在线；
 *   3. 后面任何"读不出/不显示"的问题，第一个动作就是跑它。
 */
void EE_Scan(void)
{
    char     line[48];
    uint16_t addr;
    uint8_t  found = 0;

    dbg("---- I2C1 scan (7-bit addr) ----\r\n");

    for (addr = 0x08; addr <= 0x77; addr++)
    {
        /* 注意：DevAddress 传的是 7 位地址 << 1（8 位形式） */
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 3, 10) == HAL_OK)
        {
            sprintf(line, "  found: 0x%02X\r\n", (unsigned)addr);
            dbg(line);
            found++;
        }
    }

    sprintf(line, "---- total %u device(s) ----\r\n", (unsigned)found);
    dbg(line);
}

/**
 * @brief  回读校验自测（模块 1 的第 1 项 + 第 3 项）
 *
 * 分两段跑，串口各打印一行结果：
 *   ① 单字节写 / 单字节读：写 0x20 和 0x21，读回比对
 *   ② 多字节连续写 / 连续读：从 0x20 起写 16 字节（必然跨页），整块读回逐一比对
 *
 * 为什么第②段从 0x20 开始：AT24C02 一页只有 8 字节，16 字节必然跨越多页。
 *   如果 EE_WriteBuf 写成了"一次写 16 字节不分页"，就会在页边界处静默覆盖 —— 这里必现。
 */
#define EE_ADDR_SELFTEST   0x20
#define EE_SELFTEST_LEN    16

uint8_t EE_SelfTest(void)
{
    uint8_t  wr[EE_SELFTEST_LEN];
    uint8_t  rd[EE_SELFTEST_LEN];
    uint8_t  b0 = 0x00, b1 = 0x00;
    uint8_t  i;
    char     line[64];

    /* ============ ① 单字节写入与读取 ============ */
    if (EE_WriteByte(EE_ADDR_SELFTEST,     0x11) != HAL_OK ||
        EE_WriteByte(EE_ADDR_SELFTEST + 1, 0x22) != HAL_OK)
    {
        dbg("[selftest] single-byte WRITE failed\r\n");
        return 0;
    }

    if (EE_ReadByte(EE_ADDR_SELFTEST,     &b0) != HAL_OK ||
        EE_ReadByte(EE_ADDR_SELFTEST + 1, &b1) != HAL_OK)
    {
        dbg("[selftest] single-byte READ failed\r\n");
        return 0;
    }

    if (b0 != 0x11 || b1 != 0x22)
    {
        sprintf(line, "[selftest] single-byte MISMATCH: expect 11 22, got %02X %02X\r\n",
                (unsigned)b0, (unsigned)b1);
        dbg(line);
        return 0;
    }

    dbg("[selftest] step1 PASS (single byte write/read)\r\n");

    /* ============ ② 多字节连续写入与读取（跨页） ============ */
    /* 造数据：每个字节都不一样，错了才看得出来是哪一位 */
    for (i = 0; i < EE_SELFTEST_LEN; i++)
    {
        wr[i] = (uint8_t)(0xA0 + i);
        rd[i] = 0x00;
    }

    if (EE_WriteBuf(EE_ADDR_SELFTEST, wr, EE_SELFTEST_LEN) != HAL_OK)
    {
        dbg("[selftest] buffer WRITE failed\r\n");
        return 0;
    }

    if (EE_ReadBuf(EE_ADDR_SELFTEST, rd, EE_SELFTEST_LEN) != HAL_OK)
    {
        dbg("[selftest] buffer READ failed\r\n");
        return 0;
    }

    /* 逐字节比对，第一个不一致就报出来 */
    for (i = 0; i < EE_SELFTEST_LEN; i++)
    {
        if (wr[i] != rd[i])
        {
            sprintf(line, "[selftest] buffer MISMATCH @%u: wrote 0x%02X read 0x%02X\r\n",
                    (unsigned)i, (unsigned)wr[i], (unsigned)rd[i]);
            dbg(line);
            return 0;
        }
    }

    dbg("[selftest] step2 PASS (16 bytes, page-crossing write/read)\r\n");
    return 1;
}
