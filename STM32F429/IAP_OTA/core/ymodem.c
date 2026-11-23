/**
 ******************************************************************************
 * @file    ymodem.c
 * @brief   YMODEM 接收端状态机实现
 ******************************************************************************
 */
#include "ymodem.h"

#include <string.h>

#define YM_SOH      0x01u
#define YM_STX      0x02u
#define YM_EOT      0x04u
#define YM_CAN      0x18u

uint16_t Ym_Crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0u;
    uint32_t i;
    int      b;

    if (data == 0) { return 0; }

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0; b < 8; b++)
        {
            if ((crc & 0x8000u) != 0u) { crc = (uint16_t)((crc << 1) ^ 0x1021u); }
            else                       { crc = (uint16_t)(crc << 1); }
        }
    }
    return crc;
}

void Ym_Init(YmCtx *ctx, YmWriteFn write, void *user, uint32_t max_size)
{
    if (ctx == 0) { return; }

    memset(ctx, 0, sizeof(*ctx));

    ctx->state    = YM_ST_IDLE;
    ctx->expected = 0u;
    ctx->write    = write;
    ctx->user     = user;
    ctx->max_size = max_size;
}

/* 解析第 0 包： "文件名\0" + "大小(八进制)\0" */
static void parse_header(YmCtx *ctx)
{
    uint16_t i = 0;
    uint16_t n = 0;

    /* 文件名 */
    while (i < ctx->need && ctx->buf[i] != 0u && n < YM_NAME_MAX - 1u)
    {
        ctx->filename[n++] = (char)ctx->buf[i++];
    }
    ctx->filename[n] = '\0';

    /* 跳过 '\0' */
    if (i < ctx->need && ctx->buf[i] == 0u) { i++; }

    /* 大小：YMODEM 用八进制 ASCII 字符串 */
    {
        uint32_t v = 0u;

        while (i < ctx->need && ctx->buf[i] >= '0' && ctx->buf[i] <= '7')
        {
            v = v * 8u + (uint32_t)(ctx->buf[i] - '0');
            i++;
        }
        ctx->filesize = v;
    }
}

/* 一包收完：校验 seq + CRC，然后回调写数据 */
static YmReply finish_packet(YmCtx *ctx)
{
    uint16_t crc_calc;
    uint16_t crc_recv;
    uint8_t  seq_claimed = ctx->seq;

    /* seq 和 ~seq 必须互补 */
    if ((uint8_t)(seq_claimed ^ 0xFFu) != (uint8_t)(ctx->seq ^ 0xFFu))
    {
        /* 这里只是形式检查，实际互补校验在读 ~seq 时已经做过 */
    }

    /* CRC16 只覆盖数据段 */
    crc_calc = Ym_Crc16(ctx->buf, ctx->need);
    crc_recv = (uint16_t)(((uint16_t)ctx->crc_val[0] << 8) | ctx->crc_val[1]);

    if (crc_calc != crc_recv)
    {
        ctx->last_result = 1u;
        return YM_REPLY_NAK;            /* 让对端重发这一包 */
    }

    /* ---- 文件头（seq 0）---- */
    if (seq_claimed == 0u && !ctx->got_header)
    {
        /* 全 0 的空头 = 批次结束（YMODEM 的收尾）*/
        if (ctx->buf[0] == 0u)
        {
            ctx->state = YM_ST_DONE;
            return YM_REPLY_ACK;
        }

        parse_header(ctx);
        ctx->got_header = 1u;
        ctx->expected   = 1u;
        ctx->state      = YM_ST_IDLE;

        /* ACK 头包，再发 'C' 让对方开始发数据 */
        return YM_REPLY_ACK;
    }

    /* ---- 重复包：ACK 但不重复写 ---- */
    if (seq_claimed != ctx->expected)
    {
        if (seq_claimed == (uint8_t)(ctx->expected - 1u))
        {
            ctx->last_result = 2u;      /* 记录：收到重复包 */
            ctx->state = YM_ST_IDLE;
            return YM_REPLY_ACK;        /* 关键：不写 Flash */
        }

        /* 序号乱跳（跳过头或倒退太多）*/
        ctx->last_result = 3u;
        ctx->state = YM_ST_ABORT;
        return YM_REPLY_ABORT;
    }

    /* ---- 数据包：写 ---- */
    if (ctx->write != 0)
    {
        if (ctx->write(ctx->user, ctx->buf, ctx->need) != 0)
        {
            ctx->state = YM_ST_ABORT;
            return YM_REPLY_ABORT;
        }
    }

    ctx->bytes_written += ctx->need;
    ctx->expected       = (uint8_t)(ctx->expected + 1u);
    ctx->state          = YM_ST_IDLE;

    return YM_REPLY_ACK;
}

YmReply Ym_Feed(YmCtx *ctx, uint8_t byte)
{
    if (ctx == 0) { return YM_REPLY_ABORT; }

    /* 取消：任何时候收到 CAN 都直接中止 */
    if (byte == YM_CAN)
    {
        ctx->state = YM_ST_ABORT;
        return YM_REPLY_ABORT;
    }

    switch (ctx->state)
    {
    case YM_ST_IDLE:
        if (byte == YM_SOH)
        {
            ctx->need  = 128u;
            ctx->idx   = 0u;
            ctx->state = YM_ST_SEQ;
            return YM_REPLY_NONE;
        }
        if (byte == YM_STX)
        {
            ctx->need  = 1024u;
            ctx->idx   = 0u;
            ctx->state = YM_ST_SEQ;
            return YM_REPLY_NONE;
        }
        if (byte == YM_EOT)
        {
            ctx->state = YM_ST_DONE;
            return YM_REPLY_ACK;
        }
        return YM_REPLY_NONE;           /* 其它字节忽略 */

    case YM_ST_SEQ:
        ctx->seq   = byte;
        ctx->state = YM_ST_SEQINV;
        return YM_REPLY_NONE;

    case YM_ST_SEQINV:
        /* 互补校验：坏了就回到 IDLE 等下一包 */
        if ((uint8_t)(ctx->seq ^ byte) != 0xFFu)
        {
            ctx->last_result = 4u;
            ctx->state = YM_ST_IDLE;
            return YM_REPLY_NAK;
        }
        ctx->idx   = 0u;
        ctx->state = YM_ST_DATA;
        return YM_REPLY_NONE;

    case YM_ST_DATA:
        if (ctx->idx < ctx->need)
        {
            ctx->buf[ctx->idx++] = byte;
            return YM_REPLY_NONE;
        }
        /* 数据收满，进入 CRC 阶段；当前字节是 CRC 高字节 */
        ctx->crc_val[0] = byte;
        ctx->crc_idx    = 1u;
        ctx->state      = YM_ST_CRC;
        return YM_REPLY_NONE;

    case YM_ST_CRC:
        ctx->crc_val[1] = byte;

        /* 长度上限检查：写穿 APP 区就直接中止（安全红线）*/
        if (ctx->got_header &&
            (ctx->bytes_written + ctx->need) > ctx->max_size)
        {
            ctx->last_result = 5u;
            ctx->state = YM_ST_ABORT;
            return YM_REPLY_ABORT;
        }

        return finish_packet(ctx);

    case YM_ST_DONE:
        return YM_REPLY_DONE;

    case YM_ST_ABORT:
    default:
        return YM_REPLY_ABORT;
    }
}

const char *Ym_StateName(YmState s)
{
    switch (s)
    {
    case YM_ST_IDLE:   return "IDLE";
    case YM_ST_SEQ:    return "SEQ";
    case YM_ST_SEQINV: return "SEQINV";
    case YM_ST_DATA:   return "DATA";
    case YM_ST_CRC:    return "CRC";
    case YM_ST_DONE:   return "DONE";
    case YM_ST_ABORT:  return "ABORT";
    default:           return "?";
    }
}

uint32_t Ym_BytesWritten(const YmCtx *ctx)
{
    return (ctx == 0) ? 0u : ctx->bytes_written;
}

const char *Ym_FileName(const YmCtx *ctx)
{
    return (ctx == 0) ? "" : ctx->filename;
}

uint32_t Ym_FileSize(const YmCtx *ctx)
{
    return (ctx == 0) ? 0u : ctx->filesize;
}
