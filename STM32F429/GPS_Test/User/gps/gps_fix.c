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

/* 时间差：处理毫秒计数回绕（HAL_GetTick / Tick_Get 都是 32 位，约 49 天回绕）*/
static uint32_t elapsed(uint32_t now, uint32_t then)
{
    return (uint32_t)(now - then);
}

void GpsFix_OnLine(GpsFix *g, const char *line, uint32_t now_ms)
{
    RmcData rmc;
    int     r;

    if (g == 0 || line == 0) { return; }

    /* ① 校验和不对的语句直接丢掉，不参与任何状态判断。
     *    这一步很重要：串口错字节时宁可不更新，也不能把垃圾当定位。 */
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
        /* 有数据但还没定位过：从"无数据"变成"未定位" */
        g->state = g->had_fix ? GPS_STALE : GPS_FIX_INVALID;
    }

    /* ③ 只关心 RMC；其他语句（GSV/GGA/GSA...）只是证明链路活着 */
    r = Rmc_Parse(line, &rmc);

    if (r == 0)
    {
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

    /* ④ 更新定位状态 */
    if (rmc.valid && rmc.has_pos)
    {
        g->fix        = rmc;
        g->had_fix    = 1;
        g->last_fix_ms = now_ms;
        g->state      = GPS_FIX_VALID;
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
