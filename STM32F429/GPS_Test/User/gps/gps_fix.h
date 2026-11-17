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
 *
 * 数据来源分工（三条语句各管一块，不能互相替代）：
 *   RMC  -> 状态 A/V、经纬度、速度、UTC 时间、日期
 *   GGA  -> 定位质量、**参与定位的卫星数**、HDOP、海拔
 *   GSV  -> **可见卫星数**（第 3 段，整个周期共用）
 *   TXT  -> 模块自身的告警（例如 ANTENNA OPEN）
 *
 * 时钟是**外部注入**的（每个接口都带 now_ms），所以可以在 PC 上
 * 用假时钟把超时、恢复、反复失效全部测一遍，不需要等真的过 3 秒。
 ******************************************************************************
 */
#ifndef __GPS_FIX_H
#define __GPS_FIX_H

#include <stdint.h>

#include "gga.h"
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

    /* ---- 来自 GGA ---- */
    uint8_t  quality;           /* 定位质量原始值：0=无 1=GPS 2=DGPS 4/5=RTK */
    uint8_t  sats_used;         /* 参与定位的卫星数 */
    uint16_t hdop_x10;          /* HDOP × 10 */
    int      has_hdop;
    int32_t  alt_m;             /* 海拔（米）*/
    int      has_alt;
    int      has_gga;

    /* ---- 来自 GSV ---- */
    uint8_t  sats_visible;      /* 可见卫星数（不要求参与定位）*/
    int      has_gsv;

    /* ---- 来自 GPTXT ---- */
    int      antenna_open;      /* 1 = 曾经报过天线开路（历史记录，不清零）*/
    uint32_t antenna_warn_count;
    uint32_t last_antenna_warn_ms;  /* 最近一次天线告警的时刻 */

    uint32_t last_line_ms;      /* 最近收到任意合法语句的时刻 */
    uint32_t last_fix_ms;       /* 最近收到有效定位的时刻 */

    uint32_t stale_ms;          /* 有效定位的新鲜期（超时判定）*/
    uint32_t link_ms;           /* 多久没收到任何语句就认为链路断了 */

    uint32_t lines;             /* 合法语句总数 */
    uint32_t bad_checksum;      /* 校验和失败的条数 */
    uint32_t not_rmc;           /* 不认识的语句条数（正常，如 VTG/ZDA）*/
} GpsFix;

/** 初始化
 *  @param stale_ms  有效定位的保鲜期，建议 3000 ms
 *  @param link_ms   多久没有语句算链路断，建议 5000 ms
 */
void GpsFix_Init(GpsFix *g, uint32_t stale_ms, uint32_t link_ms);

/** 喂一条完整语句（来自 gps_line.c 组出来的行）
 *  内部顺序：校验和 -> 计数 -> 按语句名分发给 RMC/GGA/GSV/TXT -> 更新子状态
 */
void GpsFix_OnLine(GpsFix *g, const char *line, uint32_t now_ms);

/** 周期调用（建议每 100 ms 一次），负责超时判定 */
void GpsFix_Tick(GpsFix *g, uint32_t now_ms);

/** 最近一次有效定位距今多久（毫秒）。从未定位过返回 0xFFFFFFFF */
uint32_t GpsFix_AgeMs(const GpsFix *g, uint32_t now_ms);

/** 状态名（调试/显示用）*/
const char *GpsFix_StateName(GpsState s);

/** 质量值的可读名："no fix" / "GPS" / "DGPS" / "RTK" ... */
const char *GpsFix_QualityName(uint8_t quality);

/** 天线状态的判定结果 */
typedef enum
{
    GPS_ANT_UNKNOWN = 0,    /* 还没收到足够语句，无法判断 */
    GPS_ANT_OK,             /* 收了足够多的语句，期间没有天线告警 */
    GPS_ANT_OPEN            /* 模块报告 ANTENNA OPEN / SHORT */
} GpsAntenna;

/** 判定天线状态至少要收到多少条语句
 *  ATGM332D 的 ANTENNA OPEN 是周期性发的（不是每轮都有），
 *  大约 3 轮 ≈ 30 条语句之后才敢下结论。 */
#define GPS_ANTENNA_CONFIRM_LINES   30u

/** 一条天线告警的"有效期"：超过这么久没再报，就认为天线已经恢复正常。
 *
 *  没有这个时限的话，antenna_open 一旦置位就永远不清 ——
 *  用户把天线修好了，程序还是永远显示 FAIL，必须重启板子才能刷掉。
 *  加上时限之后，修好天线约 10 秒就会自动回到 OK。 */
#define GPS_ANTENNA_WARN_HOLD_MS    10000u

/** 天线状态判定（三态）
 *
 *  为什么必须是三态而不是"有告警=坏 / 没告警=好"：
 *    ANTENNA OPEN 是**周期性**发的，不是每条语句都带。
 *    刚上电时一条 TXT 都还没来，这时直接判"正常"，
 *    就会出现"第一份报告说 OK，两秒后变成 FAIL"的误导。
 *    「没收到告警」和「确认没有告警」是两件事，必须区分。
 *
 *  @param now_ms  当前时刻，用于判断最后一条告警是否已过期
 */
GpsAntenna GpsFix_Antenna(const GpsFix *g, uint32_t now_ms);

#endif /* __GPS_FIX_H */
