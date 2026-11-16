/**
 ******************************************************************************
 * @file    nmea.c
 * @brief   NMEA 校验和 + 逗号分段（纯 C，不依赖硬件）
 ******************************************************************************
 */
#include "nmea.h"

#include <string.h>

/* 十六进制字符 -> 数值，失败返回 -1 */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

int Nmea_ChecksumOk(const char *line)
{
    const char *p;
    const char *star;
    uint8_t     cs = 0;
    int         hi, lo;

    if (line == 0 || line[0] != '$') { return 0; }

    /* 找 '*'。找不到说明不是完整语句（可能被截断了）*/
    star = strchr(line, '*');
    if (star == 0) { return 0; }

    /* '*' 后面必须有两位十六进制 */
    hi = hex_val(star[1]);
    lo = hex_val(star[2]);
    if (hi < 0 || lo < 0) { return 0; }

    /* '$' 和 '*' 之间所有字节异或 */
    for (p = line + 1; p < star; p++)
    {
        cs ^= (uint8_t)(*p);
    }

    return (cs == (uint8_t)((hi << 4) | lo)) ? 1 : 0;
}

int Nmea_Split(const char *line, NmeaFields *out)
{
    const char *p;
    int         len;

    if (out == 0) { return 0; }
    if (line == 0 || line[0] != '$') { return 0; }

    out->n = 0;
    out->truncated = 0;

    p = line + 1;                   /* 跳过 '$' */

    while (*p != '\0' && *p != '*')
    {
        if (out->n >= NMEA_MAX_FIELDS)
        {
            out->truncated = 1;
            break;
        }

        /* 收一个字段，直到 ',' 或 '*' 或结尾 */
        len = 0;
        while (*p != '\0' && *p != ',' && *p != '*')
        {
            if (len < NMEA_FIELD_MAX - 1)
            {
                out->f[out->n][len++] = *p;
            }
            else
            {
                out->truncated = 1;     /* 字段超长，截断 */
            }
            p++;
        }
        out->f[out->n][len] = '\0';
        out->n++;

        if (*p == ',') { p++; }         /* 跳过逗号，继续下一段 */
    }

    return (out->n > 0) ? 1 : 0;
}

const char *Nmea_Talker(const NmeaFields *fields)
{
    if (fields == 0 || fields->n < 1) { return ""; }
    return fields->f[0];
}
