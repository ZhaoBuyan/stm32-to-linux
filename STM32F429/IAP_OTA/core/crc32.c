/**
 ******************************************************************************
 * @file    crc32.c
 * @brief   CRC-32（zlib 兼容）实现
 *
 * 用**半字节查表**（16 项），比全表（256 项 × 4 字节 = 1 KB）省 Flash，
 * 又比逐位算快一倍多。Bootloader 里 Flash 很紧张，值得。
 ******************************************************************************
 */
#include "crc32.h"

/* 反射多项式 0xEDB88320 的半字节表 */
static const uint32_t k_crc32_nibble[16] =
{
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t Crc32_Start(void)
{
    return 0xFFFFFFFFu;
}

uint32_t Crc32_Update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    if (data == 0) { return crc; }

    for (i = 0; i < len; i++)
    {
        crc ^= (uint32_t)p[i];

        /* 一个字节分两次，每次 4 位 */
        crc = (crc >> 4) ^ k_crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ k_crc32_nibble[crc & 0x0Fu];
    }
    return crc;
}

uint32_t Crc32_Finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t Crc32_Calc(const void *data, size_t len)
{
    return Crc32_Finish(Crc32_Update(Crc32_Start(), data, len));
}
