/**
 ******************************************************************************
 * @file    app_layout.h
 * @brief   Flash 分区与 APP 有效性判断（纯逻辑，可在 PC 上单测）
 *
 * ============================ 分区（DB1M = 0，单 Bank）============================
 *
 *   F429IGT6 片内 Flash 1 MiB = 0x08000000 .. 0x080FFFFF
 *
 *   扇区几何（DB1M=0）：
 *     Sector 0 ~ 3   16 KB  each   -> 0x08000000 .. 0x0800FFFF
 *     Sector 4       64 KB         -> 0x08010000 .. 0x0801FFFF
 *     Sector 5 ~ 11  128 KB each   -> 0x08020000 .. 0x080FFFFF
 *
 *   本工程分区：
 *     Bootloader  0x08000000 .. 0x0801FFFF   128 KB   = Sector 0-4
 *     APP         0x08020000 .. 0x080FFFFF   896 KB   = Sector 5-11  (7 x 128 KB)
 *
 *   ⚠️ DB1M=1（双 Bank）时扇区表**完全不同**（两个 Bank 各 512 KB，扇区全变 16 KB），
 *      照抄本文件的分区会擦错区。上板前必须用 STM32CubeProgrammer 读确认。
 *
 * ============================ APP 有效性判断 ============================
 *
 * 跳转前必须确认（任务书 P0-04）：
 *   ① 初始 MSP 落在合法 RAM 范围内
 *   ② 复位入口地址在 APP 区内，且 Thumb 位为 1
 *   ③ 整镜像状态有效（由 Manifest / 镜像 CRC 决定，见 manifest.h）
 *
 * 这三条任何一条不过，**都必须拒绝跳转**，停在 Bootloader 等重新升级。
 ******************************************************************************
 */
#ifndef __APP_LAYOUT_H
#define __APP_LAYOUT_H

#include <stdint.h>

/* ---------------- 分区 ---------------- */
#define FLASH_BASE_ADDR      0x08000000u
#define FLASH_TOTAL_SIZE     (1024u * 1024u)          /* 1 MiB */

#define BOOT_ADDR            0x08000000u
#define BOOT_SIZE            (128u * 1024u)           /* 128 KB，Sector 0-4 */

#define APP_ADDR             0x08020000u
#define APP_SIZE             (896u * 1024u)           /* 896 KB，Sector 5-11 */

#define APP_END_ADDR         (APP_ADDR + APP_SIZE)    /* 0x08100000，不含 */

/* APP 区第一个扇区号（F429 DB1M=0：Sector 5 起始 0x08020000）*/
#define APP_FIRST_SECTOR     5u
#define APP_SECTOR_COUNT     7u

/* SRAM 范围（F429IGT6：192 KB SRAM @0x20000000，另有 64 KB CCM @0x10000000）*/
#define SRAM_BASE_ADDR       0x20000000u
#define SRAM_END_ADDR        (SRAM_BASE_ADDR + 192u * 1024u)   /* 0x20030000，不含 */

/* ---------------- 结果码 ---------------- */
typedef enum
{
    APP_IMG_OK = 0,
    APP_IMG_ERR_MSP,            /* 初始 MSP 不在 SRAM 范围内（多半是空片 0xFFFFFFFF）*/
    APP_IMG_ERR_ENTRY_RANGE,    /* 复位入口不在 APP 区内 */
    APP_IMG_ERR_ENTRY_THUMB,    /* 复位入口的 Thumb 位不是 1（不是 Thumb 代码）*/
    APP_IMG_ERR_EMPTY,          /* 入口或 MSP 是 0xFFFFFFFF / 0x00000000：没烧过 */
    APP_IMG_ERR_ALIGN           /* 向量表没按 0x200 对齐（VTOR 要求）*/
} AppImgResult;

/** 判断一个 MSP 值是否落在 SRAM 内（含 CCM 可选支持）
 *  @param allow_ccm  1 = 也接受 0x10000000 起的 CCM RAM
 */
int App_IsMspValid(uint32_t msp, int allow_ccm);

/** 判断 APP 复位入口是否合法
 *  @param entry  APP_ADDR+4 处读到的值（Reset_Handler 地址）
 */
int App_IsEntryValid(uint32_t entry);

/** 综合判断：给 MSP 和 entry，返回明确结果码 */
AppImgResult App_CheckVector(uint32_t msp, uint32_t entry);

/** 判断地址区间是否完全落在 APP 区内（擦写范围检查用，P0-06）
 *  @return 1 = 合法；0 = 越界 / 零长度 / 溢出
 */
int App_IsRangeInApp(uint32_t addr, uint32_t len);

/** 结果码的可读名（打印用）*/
const char *App_ImgResultName(AppImgResult r);

#endif /* __APP_LAYOUT_H */
