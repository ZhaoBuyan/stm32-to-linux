/**
 ******************************************************************************
 * @file    rmc.c
 * @brief   GPRMC/GNRMC 解析实现（纯 C，不依赖硬件，全部整数运算）
 *
 * 为什么全程整数：
 *   F429 有 FPU，但解析器要能在 PC 上跑单测、也要能移植到没有 FPU 的片子。
 *   经纬度用 ×1e7 定点，速度用 ×1000 定点，精度够用且完全确定。
 ******************************************************************************
 */
#include "rmc.h"

#include <string.h>

/* ---------------- 小工具 ---------------- */

static int is_digit(char c) { return (c >= '0' && c <= '9'); }

/* 把 "hhmmss.ss" 转成当天毫秒。失败返回 -1 */
static int32_t parse_utc_ms(const char *s)
{
    int hh, mm, ss, cs;

    if (s == 0 || !is_digit(s[0]) || !is_digit(s[1]) ||
        !is_digit(s[2]) || !is_digit(s[3]) ||
        !is_digit(s[4]) || !is_digit(s[5]))
    {
        return -1;
    }

    hh = (s[0] - '0') * 10 + (s[1] - '0');
    mm = (s[2] - '0') * 10 + (s[3] - '0');
    ss = (s[4] - '0') * 10 + (s[5] - '0');

    if (hh > 23 || mm > 59 || ss > 60) { return -1; }

    /* 小数部分：最多取两位（百分之一秒）*/
    cs = 0;
    if (s[6] == '.' && is_digit(s[7]))
    {
        cs = (s[7] - '0') * 10;
        if (is_digit(s[8])) { cs += (s[8] - '0'); }
    }

    return (int32_t)(((int32_t)hh * 3600 + (int32_t)mm * 60 + (int32_t)ss) * 1000
                     + cs * 10);
}

/* "ddmmyy" -> 年月日。失败返回 0 */
static int parse_date(const char *s, uint16_t *y, uint8_t *mo, uint8_t *d)
{
    int dd, mm, yy;

    if (s == 0 ||
        !is_digit(s[0]) || !is_digit(s[1]) ||
        !is_digit(s[2]) || !is_digit(s[3]) ||
        !is_digit(s[4]) || !is_digit(s[5]))
    {
        return 0;
    }

    dd = (s[0] - '0') * 10 + (s[1] - '0');
    mm = (s[2] - '0') * 10 + (s[3] - '0');
    yy = (s[4] - '0') * 10 + (s[5] - '0');

    if (dd < 1 || dd > 31 || mm < 1 || mm > 12) { return 0; }

    *y  = (uint16_t)(2000 + yy);
    *mo = (uint8_t)mm;
    *d  = (uint8_t)dd;
    return 1;
}

/* "12.345" -> 12.345 节，返回毫节（×1000）。空串返回 0 并标记失败 */
static int parse_speed_milli_knots(const char *s, uint32_t *out)
{
    uint32_t ip = 0, fp = 0, scale = 1;
    int      digits = 0;

    if (s == 0 || s[0] == '\0') { return 0; }

    while (is_digit(*s))
    {
        if (ip > 100000u) { return 0; }         /* 防溢出：>100000 节不合理 */
        ip = ip * 10u + (uint32_t)(*s - '0');
        s++;
    }

    if (*s == '.')
    {
        s++;
        while (is_digit(*s) && scale < 1000u)
        {
            fp = fp * 10u + (uint32_t)(*s - '0');
            scale *= 10u;
            digits++;
            s++;
        }
        /* 后面的多余小数位直接吃掉 */
        while (is_digit(*s)) { s++; }
    }

    if (*s != '\0') { return 0; }               /* 有非法字符 */

    /* 补齐到千分之一 */
    while (scale < 1000u) { fp *= 10u; scale *= 10u; }

    (void)digits;
    *out = ip * 1000u + fp;
    return 1;
}

/* ---------------- 对外接口 ---------------- */

int Rmc_DdmmToE7(const char *s, int32_t *out_e7)
{
    /* 形如 "3150.70613"（纬度）或 "11711.90887"（经度）
     *   整数部分末两位是"分"，前面是"度"
     *   小数部分是分的零头
     * 做法：先按 1e5 定点算出"分"，再 /60 换成度。全程整数。 */
    uint32_t int_part = 0;
    uint32_t frac_x1e5 = 0;
    uint32_t scale = 1;
    uint32_t deg, minutes_x1e5;
    int32_t  deg_x1e7;

    if (s == 0 || out_e7 == 0 || !is_digit(s[0])) { return 0; }

    while (is_digit(*s))
    {
        if (int_part > 18000u) { return 0; }    /* 度分合计不会这么大 */
        int_part = int_part * 10u + (uint32_t)(*s - '0');
        s++;
    }

    if (*s == '.')
    {
        s++;
        while (is_digit(*s) && scale < 100000u)
        {
            frac_x1e5 = frac_x1e5 * 10u + (uint32_t)(*s - '0');
            scale *= 10u;
            s++;
        }
        while (is_digit(*s)) { s++; }           /* 多余位数丢掉 */
    }

    if (*s != '\0') { return 0; }

    while (scale < 100000u) { frac_x1e5 *= 10u; scale *= 10u; }

    /* int_part = ddmm 或 dddmm：末两位是分 */
    deg = int_part / 100u;
    minutes_x1e5 = (int_part % 100u) * 100000u + frac_x1e5;

    /* 纬度最大 90 度，经度最大 180 度；这里统一按 180 检查 */
    if (deg > 180u) { return 0; }

    /* 度 -> ×1e7；分 -> 度：minutes_x1e5 / 60 * 1e7 = minutes_x1e5 * 5 / 3
     * 用四舍五入： (minutes_x1e5 * 5 + 1) / 3 */
    deg_x1e7 = (int32_t)(deg * 10000000u)
             + (int32_t)((minutes_x1e5 * 5u + 1u) / 3u);

    if (deg_x1e7 < -1800000000 || deg_x1e7 > 1800000000) { return 0; }

    *out_e7 = deg_x1e7;
    return 1;
}

int Rmc_Parse(const char *line, RmcData *out)
{
    NmeaFields f;

    if (out == 0) { return 0; }

    /* 先清成"什么都没有"的安全状态 */
    memset(out, 0, sizeof(*out));

    if (!Nmea_Split(line, &f)) { return 0; }

    /* 语句名必须是 RMC 结尾（GNRMC / GPRMC / BDRMC 都接受）*/
    if (f.n < 1) { return 0; }
    {
        const char *name = f.f[0];
        size_t      len = strlen(name);
        if (len < 3 || strcmp(name + len - 3, "RMC") != 0) { return 0; }
    }

    /* RMC 完整时有 13 段（老版本 12 段，没有末尾的模式字段）。
     * 少于 10 段说明这条语句被截断了（串口丢字节 / 缓冲溢出），
     * 后面按字段下标取值全部会错位，所以直接判为坏数据。
     * 下限取 10 而不是 13，是为了兼容没有 mode 字段的旧接收机。 */
    if (f.n < 10) { return -1; }

    /* --- 状态：A = 有效，V = 无效，其他算无效 --- */
    out->valid = (f.f[2][0] == 'A') ? 1 : 0;

    /* --- 时间 --- */
    {
        int32_t ms = parse_utc_ms(f.f[1]);
        if (ms >= 0)
        {
            out->utc_ms_of_day = (uint32_t)ms;
            out->has_time = 1;
        }
    }

    /* --- 经纬度 ---
     * 关键：只有状态 A **且** 两个字段都非空才认为有位置。
     * 状态 V 时字段通常是空的，如果硬按 0 处理，就会在地图上画出一个
     * "几内亚湾"的假点 —— 这是 GPS 解析最典型的错误。 */
    if (f.n >= 7 && out->valid)
    {
        int32_t lat = 0, lon = 0;

        if (Rmc_DdmmToE7(f.f[3], &lat) && Rmc_DdmmToE7(f.f[5], &lon))
        {
            /* 方向：N/S、E/W */
            if (f.f[4][0] == 'N' || f.f[4][0] == 'S')
            {
                if (f.f[6][0] == 'E' || f.f[6][0] == 'W')
                {
                    if (f.f[4][0] == 'S') { lat = -lat; }
                    if (f.f[6][0] == 'W') { lon = -lon; }

                    out->lat_e7 = lat;
                    out->lon_e7 = lon;
                    out->has_pos = 1;
                }
            }
        }
    }

    /* --- 速度：节 -> km/h ---
     * 1 节 = 1.852 km/h
     * 毫节 × 1852 / 1000 = km/h × 1000 */
    if (f.n >= 8)
    {
        uint32_t milli_knots = 0;
        if (parse_speed_milli_knots(f.f[7], &milli_knots))
        {
            out->speed_kmh_x1000 = (uint32_t)((milli_knots * 1852u + 500u) / 1000u);
        }
    }

    /* --- 日期 --- */
    if (f.n >= 10)
    {
        out->has_date = parse_date(f.f[9], &out->year, &out->month, &out->day);
    }

    return 1;
}
