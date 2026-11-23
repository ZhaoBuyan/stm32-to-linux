/**
 ******************************************************************************
 * @file    manifest.c
 * @brief   Manifest v1 解析与校验实现（纯 C，小端手工拼装）
 *
 * 为什么不用结构体直接强转：
 *   结构体会被编译器插入对齐填充（padding），32 字节的包可能变成 36 字节；
 *   而且要依赖编译器的字节序和打包指令（#pragma pack）。
 *   手工按偏移读写虽然啰嗦，但**在任何编译器、任何字节序下行为都确定**，
 *   这是固件协议解析该有的写法。
 ******************************************************************************
 */
#include "manifest.h"

#include "app_layout.h"
#include "crc32.h"

#include <stdio.h>

/* ---------------- 小端读写 ---------------- */
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

int Manifest_HeaderCrcOk(const uint8_t *raw)
{
    uint32_t calc;
    uint32_t claim;

    if (raw == 0) { return 0; }

    /* 只算前 28 字节 */
    calc  = Crc32_Calc(raw, MANIFEST_SIZE - 4u);
    claim = rd32(raw + 28);

    return (calc == claim) ? 1 : 0;
}

ManifestResult Manifest_Parse(const uint8_t *raw, Manifest *out,
                              uint32_t expected_board)
{
    Manifest m;

    if (raw == 0) { return MF_ERR_SIZE; }

    /* ① magic */
    if (raw[0] != MANIFEST_MAGIC0 || raw[1] != MANIFEST_MAGIC1 ||
        raw[2] != MANIFEST_MAGIC2 || raw[3] != MANIFEST_MAGIC3)
    {
        return MF_ERR_MAGIC;
    }

    m.format_ver = rd16(raw + 4);
    m.header_len = rd16(raw + 6);

    if (m.format_ver != MANIFEST_FORMAT_VER) { return MF_ERR_FORMAT_VER; }
    if (m.header_len != MANIFEST_SIZE)       { return MF_ERR_HEADER_LEN; }

    /* ② 清单自身的 CRC（先验它，再信里面的字段）*/
    if (!Manifest_HeaderCrcOk(raw)) { return MF_ERR_HEADER_CRC; }

    m.magic[0]   = raw[0]; m.magic[1] = raw[1];
    m.magic[2]   = raw[2]; m.magic[3] = raw[3];
    m.board_id   = rd32(raw + 8);
    m.firmware_ver = rd32(raw + 12);
    m.image_len  = rd32(raw + 16);
    m.image_crc32 = rd32(raw + 20);
    m.flags      = rd32(raw + 24);
    m.header_crc32 = rd32(raw + 28);

    /* ③ 板型 */
    if (expected_board != 0u && m.board_id != expected_board)
    {
        return MF_ERR_BOARD;
    }

    /* ④ 镜像长度：不能为 0，不能超过 APP 区 */
    if (m.image_len == 0u || m.image_len > APP_SIZE)
    {
        return MF_ERR_IMAGE_LEN;
    }

    /* ⑤ 镜像 CRC 不能是 0 —— 0 表示"没填"，几乎不可能是真的 CRC */
    if (m.image_crc32 == 0u) { return MF_ERR_IMAGE_CRC_ZERO; }

    if (out != 0) { *out = m; }
    return MF_OK;
}

void Manifest_Build(uint8_t *out, uint32_t board_id, uint32_t firmware_ver,
                    uint32_t image_len, uint32_t image_crc32, uint32_t flags)
{
    if (out == 0) { return; }

    out[0] = MANIFEST_MAGIC0;
    out[1] = MANIFEST_MAGIC1;
    out[2] = MANIFEST_MAGIC2;
    out[3] = MANIFEST_MAGIC3;

    wr16(out + 4,  (uint16_t)MANIFEST_FORMAT_VER);
    wr16(out + 6,  (uint16_t)MANIFEST_SIZE);
    wr32(out + 8,  board_id);
    wr32(out + 12, firmware_ver);
    wr32(out + 16, image_len);
    wr32(out + 20, image_crc32);
    wr32(out + 24, flags);

    /* 最后算 header_crc32（必须放在其他字段都写完之后）*/
    wr32(out + 28, Crc32_Calc(out, MANIFEST_SIZE - 4u));
}

void Manifest_VersionStr(uint32_t ver, char *buf, int n)
{
    unsigned major = (unsigned)((ver >> 24) & 0xFFu);
    unsigned minor = (unsigned)((ver >> 16) & 0xFFu);
    unsigned build = (unsigned)(ver & 0xFFFFu);

    if (buf == 0 || n <= 0) { return; }

    snprintf(buf, (size_t)n, "%u.%u.%u", major, minor, build);
}

const char *Manifest_ResultName(ManifestResult r)
{
    switch (r)
    {
    case MF_OK:                  return "ok";
    case MF_ERR_SIZE:            return "buffer too small";
    case MF_ERR_MAGIC:           return "bad magic";
    case MF_ERR_FORMAT_VER:      return "unsupported format version";
    case MF_ERR_HEADER_LEN:      return "bad header_len";
    case MF_ERR_HEADER_CRC:      return "header crc mismatch";
    case MF_ERR_BOARD:           return "wrong board id";
    case MF_ERR_IMAGE_LEN:       return "bad image_len";
    case MF_ERR_IMAGE_CRC_ZERO:  return "image_crc32 is zero";
    default:                     return "?";
    }
}
