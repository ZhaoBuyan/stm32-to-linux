/**
 ******************************************************************************
 * @file    gga.c
 * @brief   GGA 解析实现（纯 C，整数运算）
 ******************************************************************************
 */
#include "gga.h"

#include <string.h>

static int is_digit(char c) { return (c >= '0' && c <= '9'); }

/* "0.9" -> 9（×10）。空串或非法返回 0 */
static int parse_hdop_x10(const char *s, uint16_t *out)
{
    uint32_t ip = 0;

    if (s == 0 || s[0] == '\0') { return 0; }

    while (is_digit(*s))
    {
        if (ip > 1000u) { return 0; }
        ip = ip * 10u + (uint32_t)(*s - '0');
        s++;
    }

    if (*s == '.')
    {
        s++;
        if (is_digit(*s)) { ip = ip * 10u + (uint32_t)(*s - '0'); }
        while (is_digit(*s)) { s++; }       /* 多余小数位丢掉 */
    }

    if (*s != '\0') { return 0; }
    if (ip > 65535u) { return 0; }

    *out = (uint16_t)ip;
    return 1;
}

/* "45.6" -> 45（米，四舍五入到整数）。空串返回 0 */
static int parse_alt(const char *s, int32_t *out)
{
    int32_t  sign = 1;
    uint32_t ip = 0;
    uint32_t frac = 0;
    uint32_t scale = 1;

    if (s == 0 || s[0] == '\0') { return 0; }

    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    if (!is_digit(*s)) { return 0; }

    while (is_digit(*s))
    {
        if (ip > 100000u) { return 0; }
        ip = ip * 10u + (uint32_t)(*s - '0');
        s++;
    }

    if (*s == '.')
    {
        s++;
        if (is_digit(*s))
        {
            frac = (uint32_t)(*s - '0');
            scale = 10;
            s++;
        }
        while (is_digit(*s)) { s++; }
    }

    if (*s != '\0') { return 0; }

    /* 四舍五入：frac/scale >= 0.5 就进 1 */
    if (scale > 1 && frac * 2 >= scale) { ip += 1; }

    *out = sign * (int32_t)ip;
    return 1;
}

int Gga_Parse(const char *line, GgaData *out)
{
    NmeaFields f;

    if (out == 0) { return 0; }

    memset(out, 0, sizeof(*out));

    if (!Nmea_Split(line, &f)) { return 0; }
    if (f.n < 1) { return 0; }

    /* 语句名必须以 GGA 结尾（GPGGA / GNGGA / BDGGA）*/
    {
        const char *name = f.f[0];
        size_t      len = strlen(name);
        if (len < 3 || strcmp(name + len - 3, "GGA") != 0) { return 0; }
    }

    /* 完整 GGA 有 15 段；少于 8 段连卫星数都拿不到 */
    if (f.n < 8) { return -1; }

    /* 定位质量（第 6 段）：空 = 无定位，'0' = 无定位，>0 = 有定位 */
    if (f.f[6][0] != '\0')
    {
        out->quality = (uint8_t)(f.f[6][0] - '0');
        out->has_fix = (out->quality > 0) ? 1 : 0;
    }

    /* 卫星数（第 7 段）*/
    if (f.f[7][0] != '\0' && is_digit(f.f[7][0]))
    {
        unsigned v = 0;
        const char *s = f.f[7];
        while (is_digit(*s)) { v = v * 10u + (unsigned)(*s - '0'); s++; }
        if (v <= 255u) { out->sats_used = (uint8_t)v; }
    }

    /* HDOP（第 8 段）*/
    if (f.n >= 9)
    {
        out->has_hdop = parse_hdop_x10(f.f[8], &out->hdop_x10);
    }

    /* 海拔（第 9 段）*/
    if (f.n >= 11)
    {
        out->has_alt = parse_alt(f.f[9], &out->alt_m);
    }

    return 1;
}
