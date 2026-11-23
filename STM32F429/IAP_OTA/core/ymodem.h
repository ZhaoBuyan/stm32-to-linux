/**
 ******************************************************************************
 * @file    ymodem.h
 * @brief   YMODEM 接收端协议状态机（纯 C，逐字节喂入，可在 PC 上单测）
 *
 * 协议要点（接收端视角）：
 *
 *   包格式：
 *     SOH(0x01) | seq | ~seq | 128 字节数据 | CRC16 高 | CRC16 低
 *     STX(0x02) | seq | ~seq | 1024 字节数据 | CRC16 高 | CRC16 低
 *     单字节控制：EOT(0x04) 结束  CAN(0x18) 取消
 *
 *   序号从 0 开始，**逐包递增**；seq 和 ~seq 必须互补，否则包坏了。
 *   没收到数据时接收方周期性发 'C'(0x43)，表示"我要 CRC 模式"。
 *
 *   第 0 包是**文件头**，不写 Flash：内容为
 *     "文件名\0" + "文件大小(八进制 ASCII)\0" + 填充
 *   第 1 包起才是真正的数据。
 *
 * 三个容易做错、这里专门处理了的地方：
 *
 *   ① **重复包**：seq 等于"上一个已收包"时，要回 ACK 但**不能重复写入**。
 *      串口在 ACK 丢失时会重发同一包，重复写会把数据错位。
 *
 *   ② **长度上限**：累计写入字节数不能超过调用方给的 max_size，
 *      否则会写穿到 Bootloader 区（任务书第 23 行的安全红线）。
 *
 *   ③ **CRC16 用 XMODEM 参数**：poly 0x1021，初值 0，只覆盖数据段，
 *      不含 SOH/seq/~seq。
 ******************************************************************************
 */
#ifndef __YMODEM_H
#define __YMODEM_H

#include <stdint.h>

#define YM_DATA_MAX     1024u
#define YM_NAME_MAX     64u

/* 喂一个字节后，调用方需要做什么 */
typedef enum
{
    YM_REPLY_NONE = 0,      /* 什么都不用发，继续喂 */
    YM_REPLY_ASK_C,         /* 发 'C'(0x43)：请对方开始发 / 继续发 */
    YM_REPLY_ACK,           /* 发 0x06 */
    YM_REPLY_NAK,           /* 发 0x15 */
    YM_REPLY_DONE,          /* 传输结束，校验和比对交给调用方 */
    YM_REPLY_ABORT          /* 出错，发 CAN CAN 中止 */
} YmReply;

/* 数据回调：把第 offset 字节起的数据写进 Flash
 * @return 0 成功；非 0 失败（调用方会把它变成 YM_REPLY_ABORT） */
typedef int (*YmWriteFn)(void *user, const uint8_t *data, uint32_t len);

typedef enum
{
    YM_ST_IDLE = 0,         /* 等 SOH/STX/EOT */
    YM_ST_SEQ,              /* 等 seq */
    YM_ST_SEQINV,           /* 等 ~seq */
    YM_ST_DATA,             /* 收数据 */
    YM_ST_CRC,              /* 收 CRC16 两个字节 */
    YM_ST_DONE,
    YM_ST_ABORT
} YmState;

typedef struct
{
    /* 收包缓冲：多留几个字节防止写越界 */
    uint8_t   buf[YM_DATA_MAX + 4u];
    uint16_t  need;             /* 本包数据长度（128 或 1024）*/
    uint16_t  idx;              /* 当前收了多少 */
    uint8_t   crc_idx;
    uint8_t   crc_val[2];

    YmState   state;
    uint8_t   seq;              /* 本包声明的 seq */
    uint8_t   expected;         /* 期望的下一个 seq */
    uint8_t   got_header;       /* 是否已收到文件头 */
    uint8_t   last_result;      /* 最近一次的结果码（调试用）*/

    uint32_t  max_size;         /* 允许写入的最大字节数 */
    uint32_t  bytes_written;

    char      filename[YM_NAME_MAX];
    uint32_t  filesize;         /* 从文件头解析出的十进制大小（0 = 未知）*/

    YmWriteFn write;
    void     *user;
} YmCtx;

/** 初始化
 *  @param max_size  允许写入的最大字节数（一般传 APP_SIZE）
 */
void Ym_Init(YmCtx *ctx, YmWriteFn write, void *user, uint32_t max_size);

/** 喂一个字节 */
YmReply Ym_Feed(YmCtx *ctx, uint8_t byte);

/** 当前状态（调试/显示用）*/
const char *Ym_StateName(YmState s);

/** 已写入的字节数 */
uint32_t Ym_BytesWritten(const YmCtx *ctx);

/** 解析出的文件名与大小（文件头到达后才有效）*/
const char *Ym_FileName(const YmCtx *ctx);
uint32_t Ym_FileSize(const YmCtx *ctx);

/** CRC16-XMODEM（poly 0x1021，init 0），供测试或其他模块复用 */
uint16_t Ym_Crc16(const uint8_t *data, uint32_t len);

#endif /* __YMODEM_H */
