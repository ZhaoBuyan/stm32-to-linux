/**
 ******************************************************************************
 * @file    gga.h
 * @brief   GPGGA/GNGGA 解析 —— 卫星数、定位质量、HDOP、海拔（纯 C）
 *
 * 为什么要解析 GGA，明明已经有 RMC 了：
 *   RMC 告诉你"有没有定位、在哪、多快"，
 *   但**卫星数**和**定位质量**只有 GGA 有。用户最想看的就是"几颗星"。
 *
 * GGA 字段：
 *   0 语句名   1 UTC 时间   2 纬度   3 N/S   4 经度   5 E/W
 *   6 定位质量（0=无定位 1=GPS 2=DGPS 4=RTK 5=RTK浮动 6=推算）
 *   7 使用中的卫星数（00~99）  8 HDOP  9 海拔  10 单位
 *   11 大地水准面差距  12 单位  13 差分龄期  14 基站号
 ******************************************************************************
 */
#ifndef __GGA_H
#define __GGA_H

#include <stdint.h>

#include "nmea.h"

typedef struct
{
    int      has_fix;           /* 1 = 质量字段 > 0（接收机认为已定位）*/
    uint8_t  quality;           /* 原始质量值 */
    uint8_t  sats_used;         /* 参与定位的卫星数 */
    uint16_t hdop_x10;          /* HDOP × 10（0.9 -> 9）*/
    int      has_hdop;
    int32_t  alt_m;             /* 海拔（米）*/
    int      has_alt;
} GgaData;

/** 解析一条 GGA
 *  @return  1 = 是 GGA 且解析成功
 *           0 = 不是 GGA
 *          -1 = 是 GGA 但字段不足
 */
int Gga_Parse(const char *line, GgaData *out);

#endif /* __GGA_H */
