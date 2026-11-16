/**
 ******************************************************************************
 * @file    gps_line.h
 * @brief   NMEA 组行器（纯 C，不依赖任何硬件）
 *
 * 为什么单独拆出来：
 *   组行逻辑是"收到一串字节 -> 拼成完整的一行"，
 *   本质上和串口、和 MCU 都没关系。拆成纯 C 之后
 *   就能在 PC 上用 gcc 跑单元测试，不用等板子。
 *
 *   这也是任务书 M2.1 的要求：
 *   「实现 nmea_checksum_ok、rmc_parse、gps_fix_update 三个可独立测试的函数」
 *   —— 可独立测试的前提就是先把它和硬件解耦。
 ******************************************************************************
 */
#ifndef __GPS_LINE_H
#define __GPS_LINE_H

#include <stdint.h>

#define GPS_LINE_MAX   128      /* 一条 NMEA 语句的最大长度 */

typedef struct
{
    char     buf[GPS_LINE_MAX];
    uint16_t len;
    uint8_t  discard;           /* 1 = 正在丢弃超长行的残余，直到下一个行尾 */
    uint32_t overflow;          /* 超长丢弃次数，便于统计 */
} Gps_LineAsm;

/** 复位组行器（只清缓冲和长度，不动统计与丢弃状态） */
void Gps_LineReset(Gps_LineAsm *la);

/** 喂一个字节
 *  @return  1 = 组出一条完整的行，此时 la->buf 是内容（已补 '\0'）
 *           0 = 还没到行尾（或正在丢弃残余）
 *          -1 = 刚刚发生超长，本行作废（只返回一次）
 *
 *  行尾判定：'\n' 或 '\r' 都算（模块普遍发 \r\n，第二个字节会组出空行，已忽略）
 *
 *  超长处理：进入 discard 状态，**一直丢到下一个行尾**。
 *  不能只复位一次就继续收 —— 否则超长行的尾巴会拼到下一行开头，
 *  让后面所有行的校验全部失败，更难排查。
 */
int Gps_LineFeed(Gps_LineAsm *la, int byte);

/** 取当前行内容（保证 '\0' 结尾） */
const char *Gps_LineText(const Gps_LineAsm *la);

#endif /* __GPS_LINE_H */
