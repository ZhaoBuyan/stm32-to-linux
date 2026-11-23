/**
 ******************************************************************************
 * @file    manifest.h
 * @brief   课程自定义固件包清单 Manifest v1（32 字节）解析与校验
 *
 * 格式（全部**小端**，任务书第 64 行定义）：
 *
 *   偏移  长度  字段              说明
 *   0     4     magic             固定 "FOTA" (0x46 0x4F 0x54 0x41)
 *   4     2     format_ver        本格式版本，当前 = 1
 *   6     2     header_len        清单长度，当前 = 32
 *   8     4     board_id          板型标识，F429 挑战者 V2 = 0x00004292
 *   12    4     firmware_ver      版本号，见下方编码
 *   16    4     image_len         BIN 的有效字节数（不含清单、填充、协议头）
 *   20    4     image_crc32       只覆盖 image_len 指定的那部分 BIN
 *   24    4     flags             位标志，bit0 = 强制升级
 *   28    4     header_crc32      只覆盖前 28 字节（0..27）
 *
 * firmware_ver 编码：bit31:24 = major，bit23:16 = minor，bit15:0 = build
 *   例：v1.0 -> 0x01000000，v1.1 -> 0x01010000
 *
 * ⚠️ 两个 CRC 的范围不能搞错（任务书第 64 行专门强调）：
 *      header_crc32  只算前 28 字节，**不含**自己
 *      image_crc32   只算 BIN 的有效字节，**不含**清单/填充/网络头
 *    算多了或算少了，PC 和 MCU 的结果就对不上。
 *
 * ⚠️ 本文件是纯 C 逻辑，不碰 Flash —— 所以在 PC 上可以完整单测。
 ******************************************************************************
 */
#ifndef __MANIFEST_H
#define __MANIFEST_H

#include <stdint.h>

#define MANIFEST_SIZE        32u
#define MANIFEST_MAGIC0      0x46u   /* 'F' */
#define MANIFEST_MAGIC1      0x4Fu   /* 'O' */
#define MANIFEST_MAGIC2      0x54u   /* 'T' */
#define MANIFEST_MAGIC3      0x41u   /* 'A' */
#define MANIFEST_FORMAT_VER  1u
#define MANIFEST_BOARD_F429  0x00004292u
#define MANIFEST_FLAG_FORCE  0x00000001u

typedef struct
{
    uint8_t  magic[4];
    uint16_t format_ver;
    uint16_t header_len;
    uint32_t board_id;
    uint32_t firmware_ver;
    uint32_t image_len;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t header_crc32;
} Manifest;

typedef enum
{
    MF_OK = 0,
    MF_ERR_SIZE,            /* 缓冲区不足 32 字节 */
    MF_ERR_MAGIC,           /* magic 不是 "FOTA" */
    MF_ERR_FORMAT_VER,      /* format_ver 不是 1 */
    MF_ERR_HEADER_LEN,      /* header_len 不是 32 */
    MF_ERR_HEADER_CRC,      /* header_crc32 对不上 */
    MF_ERR_BOARD,           /* board_id 不是本板 */
    MF_ERR_IMAGE_LEN,       /* image_len 为 0 或超过 APP 区 */
    MF_ERR_IMAGE_CRC_ZERO   /* image_crc32 为 0（几乎肯定是没填）*/
} ManifestResult;

/** 解析并校验 32 字节清单
 *  @param raw  至少 MANIFEST_SIZE 字节
 *  @param out  解析结果（可为 NULL，只做校验）
 *  @param expected_board  期望的 board_id；传 0 表示不检查
 */
ManifestResult Manifest_Parse(const uint8_t *raw, Manifest *out,
                              uint32_t expected_board);

/** 只校验前 28 字节的 header_crc32 */
int Manifest_HeaderCrcOk(const uint8_t *raw);

/** 组装一份清单（PC 端生成固件包时用；MCU 端不用）
 *  @param out  输出缓冲，至少 32 字节
 */
void Manifest_Build(uint8_t *out, uint32_t board_id, uint32_t firmware_ver,
                    uint32_t image_len, uint32_t image_crc32, uint32_t flags);

/** 版本号格式化成 "1.1.0"（buf 至少 16 字节）*/
void Manifest_VersionStr(uint32_t ver, char *buf, int n);

const char *Manifest_ResultName(ManifestResult r);

#endif /* __MANIFEST_H */
