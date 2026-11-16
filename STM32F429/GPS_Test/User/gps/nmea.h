/**
 ******************************************************************************
 * @file    nmea.h
 * @brief   NMEA 语句的通用处理：校验和 + 逗号分段（纯 C，可 PC 单测）
 *
 * 这里只做"和具体语句无关"的部分：
 *   - `$...*CS` 的 XOR 校验
 *   - 按逗号切成字段
 * 具体语句（RMC/GGA/GSV...）的字段含义放在各自的解析器里。
 ******************************************************************************
 */
#ifndef __NMEA_H
#define __NMEA_H

#include <stdint.h>

#define NMEA_MAX_FIELDS   20    /* RMC 最多 13 段，GSA 19 段，留 20 */
#define NMEA_FIELD_MAX    16    /* 单个字段最长：经纬度 "11711.90887" 是 11 字符 */

typedef struct
{
    char     f[NMEA_MAX_FIELDS][NMEA_FIELD_MAX];
    int      n;                 /* 实际字段数（含第 0 段的语句名）*/
    uint8_t  truncated;         /* 1 = 字段数超过上限，多出来的被丢掉 */
} NmeaFields;

/** 校验和检查
 *  @return 1 = 合法；0 = 不合法（缺 '$'、缺 '*'、长度不够、校验值不匹配）
 *
 *  算法：'$' 和 '*' 之间所有字符按字节异或，和 '*' 后两位十六进制比较。
 */
int Nmea_ChecksumOk(const char *line);

/** 按逗号切分字段
 *  @return 1 = 成功；0 = 输入不是一条合法语句（缺 '$'）
 *
 *  注意：
 *    - 中间的**空字段会保留为空串**（NMEA 里空字段是正常现象，代表"无数据"）
 *    - '*' 之后的校验位不会进入字段
 *    - 字段超长会被截断，并置 truncated
 */
int Nmea_Split(const char *line, NmeaFields *out);

/** 取语句名（"$GNRMC,..." -> "GNRMC"），失败返回空串 */
const char *Nmea_Talker(const NmeaFields *fields);

#endif /* __NMEA_H */
