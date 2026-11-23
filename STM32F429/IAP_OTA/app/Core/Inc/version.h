/**
 ******************************************************************************
 * @file    version.h
 * @brief   APP 版本信息与故障计数（任务书 P0-11 要求）
 *
 * 两个关键点：
 *
 * ① 版本号必须和实际行为对应
 *    任务书第 15 行强调："必须证明升级前后业务行为变化，而不仅仅修改版本字符串"。
 *    所以 v1.0 / v1.1 的差别做在**采样周期 + LED 闪烁节奏 + 上报字段**上，
 *    版本号只是把它们标出来。
 *
 * ② 构建时间用 __DATE__ / __TIME__
 *    每次编译自动变化，是"这一版确实是新编出来的"的硬证据，
 *    比手改版本号可靠。
 ******************************************************************************
 */
#ifndef __VERSION_H
#define __VERSION_H

#include <stdint.h>

/* 版本号编码：major<<24 | minor<<16 | build，与 Manifest 的 firmware_ver 一致 */
#define FW_VERSION_V10      0x01000000u     /* 1.0.0 */
#define FW_VERSION_V11      0x01010000u     /* 1.1.0 */

/* 编译时选择版本：default v1.0；定义 APP_V11 则编成 v1.1 */
#ifdef APP_V11
  #define FW_VERSION        FW_VERSION_V11
  #define FW_VERSION_STR    "1.1.0"
  #define FW_SAMPLE_MS      200u            /* 采样周期 200 ms */
  #define FW_LED_TOGGLE_MS  200u            /* LED 快闪 */
#else
  #define FW_VERSION        FW_VERSION_V10
  #define FW_VERSION_STR    "1.0.0"
  #define FW_SAMPLE_MS      500u            /* 采样周期 500 ms */
  #define FW_LED_TOGGLE_MS  500u            /* LED 慢闪 */
#endif

/** 打印版本横幅（含构建时间）*/
void Version_PrintBanner(void);

/** 当前版本号（Manifest 用的 packed 格式）*/
uint32_t Version_Number(void);

/** 版本字符串，如 "1.1.0" */
const char *Version_String(void);

/** 故障计数：业务里出错时 +1，用于观察升级后是否更稳 */
void     Version_FaultInc(void);
uint32_t Version_FaultCount(void);

#endif /* __VERSION_H */
