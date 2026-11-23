/**
 ******************************************************************************
 * @file    flash_if.h
 * @brief   Bootloader 的 Flash 操作层（HAL）—— 所有擦写的唯一入口
 *
 * 为什么要单独一层：
 *   任务书第 23 行的安全红线要求"不得擦除 Bootloader 区 / Option Bytes /
 *   未知分区"，还要求"严禁把外部包内的地址直接当作 Flash 写地址"。
 *
 *   把这些约束集中到一个文件里，比散落在各处可靠得多：
 *     ① 每次擦写前都过 App_IsRangeInApp() 检查（来自 core/app_layout.c）
 *     ② 调用方只能传"相对 APP 起点的偏移"，**传不了绝对地址**
 *     ③ 目标地址在这里由 APP_ADDR + offset 算出来
 *
 * 另外注意：HAL_FLASH_Program 只能按 32 位字写，且地址必须 4 字节对齐。
 * 数据长度不是 4 的倍数时要补 0xFF（Flash 擦除后的状态就是 0xFF）。
 ******************************************************************************
 */
#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include <stdint.h>

typedef enum
{
    FLASHIF_OK = 0,
    FLASHIF_ERR_RANGE,      /* 偏移/长度越界，或长度为 0 */
    FLASHIF_ERR_UNLOCK,     /* 解锁失败 */
    FLASHIF_ERR_ERASE,      /* 擦除失败 */
    FLASHIF_ERR_PROGRAM,    /* 写入失败 */
    FLASHIF_ERR_VERIFY      /* 读回校验不一致 */
} FlashIfResult;

/** 擦除整个 APP 区（Sector 5..11，896 KB）
 *  这是升级前唯一允许的擦除动作，**绝不会碰 Sector 0-4**。
 */
FlashIfResult FlashIf_EraseApp(void);

/** 从 APP 起点偏移 offset 处写入 len 字节
 *  @param offset  相对 APP_ADDR 的偏移（不是绝对地址）
 *  @param data    数据
 *  @param len     字节数
 *
 *  内部会做：范围检查 -> 解锁 -> 按字写（末尾补 0xFF）-> 加锁
 */
FlashIfResult FlashIf_Write(uint32_t offset, const uint8_t *data, uint32_t len);

/** 读回校验：对 APP 起点偏移 offset 处的 len 字节算 CRC32，与 expected 比较
 *  任务书 P0-07 要求的"安装后对有效字节区间做一致性验证"。
 */
FlashIfResult FlashIf_VerifyCrc(uint32_t offset, uint32_t len, uint32_t expected_crc);

/** 直接从 Flash 读（内存映射，不需要解锁）*/
const uint8_t *FlashIf_Ptr(uint32_t offset);

/** 读 APP 区起始处 8 字节：返回 MSP 和复位入口地址
 *  @return 1 = 读成功（永远成功，有效性判断交给 App_CheckVector）
 */
int FlashIf_ReadVector(uint32_t *msp, uint32_t *entry);

const char *FlashIf_ResultName(FlashIfResult r);

#endif /* __FLASH_IF_H */
