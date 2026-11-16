/**
 ******************************************************************************
 * @file    gps_fix.h
 * @brief   GPS 定位记录 + 定位子状态机（纯 C，时钟由外部注入，可 PC 单测）
 *
 * 四种子状态（任务书 §4.1 要求）：
 *   NO_DATA      一条合法语句都没收到 —— 链路断了 / 模块没上电
 *   FIX_INVALID  在收数据，但从未定位成功（状态一直是 V）
 *   FIX_VALID    定位有效，且最近一次有效定位还在新鲜期内
 *   STALE        曾经定位成功过，但现在超过新鲜期没有新的有效定位
 *
 * FIX_INVALID 和 STALE 的区别（这是最容易做错的地方）：
 *   FIX_INVALID = **本次上电从来没定位成功过**，界面应该说"未定位，请到空旷处"
 *   STALE       = **定位过，现在失效了**，界面应该说"定位失效"，并可以把
 *                 最后一次坐标以"灰色/过期"的方式显示出来
 *   两者对用户的含义完全不同，不能合并成一个状态。
 *
 * 时钟是**外部注入**的（每个接口都带 now_ms），所以可以在 PC 上
 * 用假时钟把超时、恢复、反复失效全部测一遍，不需要等真的过 3 秒。
 ******************************************************************************
 */
#ifndef __GPS_FIX_H
#define __GPS_FIX_H

#include <stdint.h>

#include "rmc.h"

typedef enum
{
    GPS_NO_DATA = 0,
    GPS_FIX_INVALID,
    GPS_FIX_VALID,
    GPS_STALE
} GpsState;

typedef struct
{
    GpsState state;
    RmcData  last;              /* 最近一条 RMC（可能无效）*/

    RmcData  fix;               /* 最近一次**有效**的定位 */
    int      had_fix;           /* 本次上电是否成功定位过（置位后不再清）*/

    uint32_t last_line_ms;      /* 最近收到任意合法语句的时刻 */
    uint32_t last_fix_ms;       /* 最近收到有效定位的时刻 */

    uint32_t stale_ms;          /* 有效定位的新鲜期（超时判定）*/
    uint32_t link_ms;           /* 多久没收到任何语句就认为链路断了 */

    uint32_t lines;             /* 合法语句总数 */
    uint32_t bad_checksum;      /* 校验和失败的条数 */
    uint32_t not_rmc;           /* 不是 RMC 的条数（正常，GSV/GGA 等）*/
} GpsFix;

/** 初始化
 *  @param stale_ms  有效定位的保鲜期，建议 3000 ms
 *  @param link_ms   多久没有语句算链路断，建议 5000 ms
 */
void GpsFix_Init(GpsFix *g, uint32_t stale_ms, uint32_t link_ms);

/** 喂一条完整语句（来自 gps_line.c 组出来的行）
 *  内部顺序：校验和 -> 计数 -> 如果是 RMC 就解析 -> 更新子状态
 */
void GpsFix_OnLine(GpsFix *g, const char *line, uint32_t now_ms);

/** 周期调用（建议每 100 ms 一次），负责超时判定 */
void GpsFix_Tick(GpsFix *g, uint32_t now_ms);

/** 最近一次有效定位距今多久（毫秒）。从未定位过返回 0xFFFFFFFF */
uint32_t GpsFix_AgeMs(const GpsFix *g, uint32_t now_ms);

/** 状态名（调试/显示用）*/
const char *GpsFix_StateName(GpsState s);

#endif /* __GPS_FIX_H */
