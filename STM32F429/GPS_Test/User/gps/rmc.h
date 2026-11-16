/**
 ******************************************************************************
 * @file    rmc.h
 * @brief   GPRMC/GNRMC 解析（纯 C，可 PC 单测）
 *
 * RMC 字段（"$GNRMC,..."）：
 *   0 语句名        1 UTC 时间 hhmmss.ss   2 状态 A/V
 *   3 纬度 ddmm.mmmm 4 N/S                 5 经度 dddmm.mmmm
 *   6 E/W           7 地面速度（节）        8 航向
 *   9 日期 ddmmyy   10 磁偏角  11 磁偏方向  12 模式
 *
 * 几个容易错的地方，这里都处理了：
 *   - **状态 V 时经纬度是空串**，不能当 0 处理，否则会算出一个"非洲西海岸"的假点
 *   - 度分格式 ddmm.mmmm 不是十进制度，必须 deg + mm.mmmm/60
 *   - 速度单位是"节"，要 × 1.852 换成 km/h
 *   - 空字段是合法的（代表无数据），不能当解析失败
 ******************************************************************************
 */
#ifndef __RMC_H
#define __RMC_H

#include <stdint.h>

#include "nmea.h"

typedef struct
{
    int      valid;             /* 1 = 状态字段是 'A'（定位有效）*/
    int      has_pos;           /* 1 = 经纬度都非空且方向合法 */
    int32_t  lat_e7;            /* 纬度 × 1e7，负 = 南纬 */
    int32_t  lon_e7;            /* 经度 × 1e7，负 = 西经 */
    uint32_t speed_kmh_x1000;   /* 地面速度，km/h × 1000（1 节 = 1.852 km/h）*/
    uint32_t utc_ms_of_day;     /* UTC 时刻，当天毫秒 */
    int      has_time;
    int      has_date;
    uint16_t year;              /* 20xx */
    uint8_t  month;
    uint8_t  day;
} RmcData;

/** 解析一条语句
 *  @return  1 = 是 RMC 且解析成功（即使 valid=0 也算成功）
 *           0 = 不是 RMC 语句（语句名不匹配），调用方忽略即可
 *          -1 = 是 RMC 但格式坏得没法用（字段太少）
 *
 *  不检查校验和 —— 调用方应该先过 Nmea_ChecksumOk()。
 */
int Rmc_Parse(const char *line, RmcData *out);

/** 把 ddmm.mmmm / dddmm.mmmm 转成十进制度 × 1e7
 *  @return 1 = 成功；0 = 格式不对
 *
 *  例："3150.70613" -> 31*1e7 + 50.70613/60*1e7 = 318451022
 */
int Rmc_DdmmToE7(const char *s, int32_t *out_e7);

#endif /* __RMC_H */
