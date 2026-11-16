/**
 ******************************************************************************
 * @file    gps_line.c
 * @brief   NMEA 组行器实现（纯 C，可用 gcc 在 PC 上测试）
 ******************************************************************************
 */
#include "gps_line.h"

void Gps_LineReset(Gps_LineAsm *la)
{
    if (la == 0) { return; }
    la->len = 0;
    la->buf[0] = '\0';
}

const char *Gps_LineText(const Gps_LineAsm *la)
{
    if (la == 0) { return ""; }
    return la->buf;
}

int Gps_LineFeed(Gps_LineAsm *la, int byte)
{
    int is_eol;

    if (la == 0) { return 0; }

    is_eol = (byte == '\n' || byte == '\r');

    /* 正在丢弃超长行的残余：只等行尾，其余全丢 */
    if (la->discard)
    {
        if (is_eol)
        {
            la->discard = 0;        /* 对齐到行尾，下一行从头开始 */
        }
        return 0;
    }

    /* 行尾：'\n' 或 '\r' 都算结束。
     * GPS 模块普遍发 "\r\n"，第二个字节会立刻组出一个空行，
     * 这时 len == 0，直接返回 0，调用方不会打印空行。 */
    if (is_eol)
    {
        if (la->len == 0)
        {
            return 0;               /* 空行，忽略 */
        }
        la->buf[la->len] = '\0';
        la->len = 0;                /* 内容留在 buf 里给调用方读走 */
        return 1;
    }

    /* 超长：整行作废，并进入丢弃状态直到下一个行尾。
     * 不保留前半段 —— 半条 NMEA 没有意义，反而会污染解析；
     * 也不马上恢复接收 —— 否则尾巴会拼到下一行开头。 */
    if (la->len >= (uint16_t)(GPS_LINE_MAX - 1))
    {
        la->len = 0;
        la->buf[0] = '\0';
        la->discard = 1;
        la->overflow++;
        return -1;
    }

    la->buf[la->len++] = (char)byte;
    return 0;
}
