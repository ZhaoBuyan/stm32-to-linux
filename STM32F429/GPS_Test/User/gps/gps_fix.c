/**
 ******************************************************************************
 * @file    gps_fix.c
 * @brief   GPS 定位记录与子状态机实现
 ******************************************************************************
 */
#include "gps_fix.h"

#include <string.h>

void GpsFix_Init(GpsFix *g, uint32_t stale_ms, uint32_t link_ms)
{
    if (g == 0) { return; }

    memset(g, 0, sizeof(*g));

    g->state     = GPS_NO_DATA;
    g->stale_ms  = (stale_ms == 0) ? 3000u : stale_ms;
    g->link_ms   = (link_ms  == 0) ? 5000u : link_ms;
}

/* 时间差：处理毫秒计数回绕（Tick_Get 是 32 位，约 49 天回绕）*/
static uint32_t elapsed(uint32_t now, uint32_t then)
{
    return (uint32_t)(now - then);
}

/* 语句名以 suffix 结尾？"$GNRMC..." 的字段 0 是 "GNRMC"，后缀 "RMC" */
static int name_ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);

    if (n < s) { return 0; }
    return (strcmp(name + n - s, suffix) == 0) ? 1 : 0;
}

/* 在一行里找子串（用于识别 GPTXT 里的 ANTENNA OPEN）*/
static int line_contains(const char *line, const char *needle)
{
    return (strstr(line, needle) != 0) ? 1 : 0;
}

void GpsFix_OnLine(GpsFix *g, const char *line, uint32_t now_ms)
{
    NmeaFields f;
    RmcData    rmc;
    GgaData    gga;
    int        r;

    if (g == 0 || line == 0) { return; }

    /* ① 校验和不对的语句直接丢掉，不参与任何状态判断。
     *    串口错字节时宁可不更新，也不能把垃圾当定位。 */
    if (!Nmea_ChecksumOk(line))
    {
        g->bad_checksum++;
        return;
    }

    /* ② 到这说明至少收到了一条合法语句 —— 链路是通的 */
    g->lines++;
    g->last_line_ms = now_ms;

    if (g->state == GPS_NO_DATA)
    {
        g->state = g->had_fix ? GPS_STALE : GPS_FIX_INVALID;
    }

    if (!Nmea_Split(line, &f)) { return; }

    /* ======================================================================
     * ③ 按语句名分发
     * ==================================================================== */

    /* ---- GGA：卫星数、定位质量、HDOP、海拔 ---- */
    r = Gga_Parse(line, &gga);
    if (r == 1)
    {
        g->has_gga     = 1;
        g->quality     = gga.quality;
        g->sats_used   = gga.sats_used;
        g->has_hdop    = gga.has_hdop;
        g->hdop_x10    = gga.hdop_x10;
        g->has_alt     = gga.has_alt;
        g->alt_m       = gga.alt_m;
        return;                     /* GGA 不影响 RMC 状态机 */
    }

    /* ---- GSV：可见卫星数（第 3 段，整个周期共用）---- */
    if (name_ends_with(f.f[0], "GSV"))
    {
        if (f.n >= 4 && f.f[3][0] != '\0' && f.f[3][0] >= '0' && f.f[3][0] <= '9')
        {
            unsigned v = 0;
            const char *s = f.f[3];
            while (*s >= '0' && *s <= '9') { v = v * 10u + (unsigned)(*s - '0'); s++; }
            if (v <= 255u)
            {
                g->sats_visible = (uint8_t)v;
                g->has_gsv = 1;
            }
        }
        return;
    }

    /* ---- TXT：模块自身的告警/版本信息 ---- */
    if (name_ends_with(f.f[0], "TXT"))
    {
        /* 只认天线相关的告警；普通的版本信息（HW=/SW=/IC=）不算 */
        if (line_contains(line, "ANTENNA OPEN") ||
            line_contains(line, "ANTENNA SHORT"))
        {
            g->antenna_open = 1;
            g->antenna_warn_count++;
        }
        return;
    }

    /* ---- RMC：状态、坐标、速度、时间 ---- */
    r = Rmc_Parse(line, &rmc);

    if (r == 0)
    {
        /* 不认识的语句（VTG/ZDA/GLL/GSA...），只证明链路活着 */
        g->not_rmc++;
        return;
    }
    if (r < 0)
    {
        /* 是 RMC 但字段不够，当作坏数据 */
        g->not_rmc++;
        return;
    }

    g->last = rmc;

    if (rmc.valid && rmc.has_pos)
    {
        g->fix         = rmc;
        g->had_fix     = 1;
        g->last_fix_ms = now_ms;
        g->state       = GPS_FIX_VALID;
    }
    else
    {
        /* 接收机说"现在没定位"。区分两种情况：
         *   以前定位成功过 -> STALE（定位失效，但可以显示最后一次坐标）
         *   从来没成功过   -> FIX_INVALID（还没定位到）
         */
        g->state = g->had_fix ? GPS_STALE : GPS_FIX_INVALID;
    }
}

void GpsFix_Tick(GpsFix *g, uint32_t now_ms)
{
    if (g == 0) { return; }

    /* ① 链路判定：很久没收到任何语句 -> NO_DATA
     *    注意用 last_line_ms，不是 last_fix_ms：
     *    只要模块还在吐语句，即使没定位也不能算"链路断"。 */
    if (g->lines > 0 && elapsed(now_ms, g->last_line_ms) > g->link_ms)
    {
        g->state = GPS_NO_DATA;
        return;
    }

    /* ② 新鲜度判定：处于 VALID 但超过保鲜期没有新的有效定位 -> STALE */
    if (g->state == GPS_FIX_VALID &&
        elapsed(now_ms, g->last_fix_ms) > g->stale_ms)
    {
        g->state = GPS_STALE;
    }
}

uint32_t GpsFix_AgeMs(const GpsFix *g, uint32_t now_ms)
{
    if (g == 0 || !g->had_fix) { return 0xFFFFFFFFu; }
    return elapsed(now_ms, g->last_fix_ms);
}

const char *GpsFix_StateName(GpsState s)
{
    switch (s)
    {
    case GPS_NO_DATA:     return "NO_DATA";
    case GPS_FIX_INVALID: return "FIX_INVALID";
    case GPS_FIX_VALID:   return "FIX_VALID";
    case GPS_STALE:       return "STALE";
    default:              return "?";
    }
}

const char *GpsFix_QualityName(uint8_t quality)
{
    switch (quality)
    {
    case 0:  return "no fix";
    case 1:  return "GPS";
    case 2:  return "DGPS";
    case 3:  return "PPS";
    case 4:  return "RTK fixed";
    case 5:  return "RTK float";
    case 6:  return "estimated";
    default: return "?";
    }
}
