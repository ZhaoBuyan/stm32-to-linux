/**
 ******************************************************************************
 * @file    eeprom.h
 * @brief   AT24C02（I2C EEPROM，256 字节 / 8 字节一页）驱动接口
 *
 * 设计约定：
 *   - 只暴露"按字节/按块读写"，I2C 细节全藏在 eeprom.c
 *   - 上层（app.c）只看 EE_xxx 函数，不关心 HAL
 ******************************************************************************
 */
#ifndef __EEPROM_H
#define __EEPROM_H

#include "main.h"

/* ============================ 芯片参数 ============================
 * 这几个数字必须和 AT24C02 数据手册对上，不要凭印象：
 *   容量 256 字节、页大小 8 字节、写周期 tWR 最大 5 ms
 * ================================================================ */
#define AT24C02_ADDR     0xA0   /* 7 位地址 0x50 << 1 —— HAL 要求 8 位形式 */
#define AT24C02_SIZE     256    /* 总容量（字节） */
#define AT24C02_PAGE     8      /* 页大小（字节）—— 跨页必须分块写 */
#define AT24C02_TWR_MS   5      /* 写周期：每次写之后必须等它，否则下一条命令会被忽略 */

/* ======================= 参数区地址规划 =========================
 * 作业要求"3 组参数固定存储在 EEPROM 指定地址区域"——这里就是那份规划。
 * 改地址之前先想清楚：不同字段不能重叠。
 * ============================================================== */
#define EE_ADDR_MAGIC    0x00   /* 1 字节：0xA5 = 数据有效；其它值 = 未初始化 */
#define EE_ADDR_DEV_ID   0x01   /* 2 字节：设备 ID */
#define EE_ADDR_RUN_CNT  0x03   /* 4 字节：运行计数 */
#define EE_ADDR_FW_VER   0x07   /* 2 字节：固件版本号 */
#define EE_ADDR_CRC      0x09   /* 1 字节：校验值（拓展任务用，先占位） */
#define EE_PARAM_BYTES   10     /* 上面这些字段一共占 10 字节 */

/* ============================== 接口 ============================== */

/* 写 1 字节（内部含一次 5 ms 写周期等待） */
HAL_StatusTypeDef EE_WriteByte(uint8_t memAddr, uint8_t data);

/* 读 1 字节 */
HAL_StatusTypeDef EE_ReadByte(uint8_t memAddr, uint8_t *data);

/* 写一块（len 可以任意大，函数内部负责按 8 字节分页 + 等写周期）
 * 注：AT24C02 的页写指针在页内回绕，跨页必须切块，否则会静默覆盖页头数据 */
HAL_StatusTypeDef EE_WriteBuf(uint8_t memAddr, const uint8_t *buf, uint16_t len);

/* 读一块（AT24C02 支持连续读，不需要分页） */
HAL_StatusTypeDef EE_ReadBuf(uint8_t memAddr, uint8_t *buf, uint16_t len);

/* 调试用：扫描 I2C1 上 0x08~0x77 的 7 位地址，通过串口打印在线的设备。
 * 用来确认 AT24C02(0x50) / OLED(0x3C) 到底挂在哪条总线上。 */
void EE_Scan(void);

/* 自检：单字节读写 + 从 EE_ADDR_SELFTEST(0x20) 开始写 16 字节再读回比对。
 * 起点 0x20 是故意选的：16 字节必然跨页（AT24C02 一页只有 8 字节），
 * 正好用来抓"分页写入"的 bug（跨页覆盖是静默的，不会报错）。
 * 返回值：1 = 通过，0 = 失败 */
uint8_t EE_SelfTest(void);

#endif /* __EEPROM_H */
