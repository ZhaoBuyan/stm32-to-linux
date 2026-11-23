/**
 ******************************************************************************
 * @file    flash_if.c
 * @brief   Flash 操作层实现（HAL，STM32F429，DB1M=0 单 Bank）
 ******************************************************************************
 */
#include "flash_if.h"

#include "app_layout.h"
#include "crc32.h"

#include "main.h"
#include "stm32f4xx_hal.h"

/* APP 区扇区表（DB1M=0）：
 *   Sector 5..11，每个 128 KB，起始 0x08020000
 *   如果哪天芯片是 DB1M=1，这张表就错了 —— 必须重新确认（见 docs/阶段A_架构评审.md）*/
#define APP_SECTOR_SIZE     (128u * 1024u)

FlashIfResult FlashIf_EraseApp(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t              sector_error = 0;
    HAL_StatusTypeDef     st;

    if (HAL_FLASH_Unlock() != HAL_OK) { return FLASHIF_ERR_UNLOCK; }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP   | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = APP_FIRST_SECTOR;      /* 5 */
    erase.NbSectors    = APP_SECTOR_COUNT;      /* 7  */
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7~3.6V，按字编程 */

    st = HAL_FLASHEx_Erase(&erase, &sector_error);

    HAL_FLASH_Lock();

    if (st != HAL_OK) { return FLASHIF_ERR_ERASE; }

    return FLASHIF_OK;
}

FlashIfResult FlashIf_Write(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t addr;
    uint32_t i;
    uint32_t word;
    uint32_t remain;

    /* ① 范围检查：用"偏移"而不是绝对地址，从源头堵死越界写入 */
    if (data == 0) { return FLASHIF_ERR_RANGE; }
    if (!App_IsRangeInApp(APP_ADDR + offset, len)) { return FLASHIF_ERR_RANGE; }

    if (HAL_FLASH_Unlock() != HAL_OK) { return FLASHIF_ERR_UNLOCK; }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP   | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    addr   = APP_ADDR + offset;
    remain = len;

    /* ② 按 32 位字写。HAL_FLASH_Program 要求地址 4 字节对齐 */
    for (i = 0; i + 4u <= remain; i += 4u)
    {
        word = (uint32_t)data[i]
             | ((uint32_t)data[i + 1u] << 8)
             | ((uint32_t)data[i + 2u] << 16)
             | ((uint32_t)data[i + 3u] << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return FLASHIF_ERR_PROGRAM;
        }
    }

    /* ③ 末尾不足 4 字节：补 0xFF 再写一个完整的字
     *    （Flash 擦除后就是 0xFF，补 0xFF 等于没写那些字节）*/
    if (i < remain)
    {
        uint32_t k;

        word = 0xFFFFFFFFu;
        for (k = 0; i + k < remain; k++)
        {
            uint32_t shift = 8u * k;
            word &= ~(0xFFu << shift);
            word |= ((uint32_t)data[i + k] << shift);
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return FLASHIF_ERR_PROGRAM;
        }
    }

    HAL_FLASH_Lock();
    return FLASHIF_OK;
}

FlashIfResult FlashIf_VerifyCrc(uint32_t offset, uint32_t len, uint32_t expected_crc)
{
    const uint8_t *p;
    uint32_t       crc;

    if (!App_IsRangeInApp(APP_ADDR + offset, len)) { return FLASHIF_ERR_RANGE; }

    p   = (const uint8_t *)(APP_ADDR + offset);
    crc = Crc32_Calc(p, len);

    return (crc == expected_crc) ? FLASHIF_OK : FLASHIF_ERR_VERIFY;
}

const uint8_t *FlashIf_Ptr(uint32_t offset)
{
    return (const uint8_t *)(APP_ADDR + offset);
}

int FlashIf_ReadVector(uint32_t *msp, uint32_t *entry)
{
    /* Flash 是内存映射的，直接读即可，不需要解锁 */
    if (msp != 0)   { *msp   = *(volatile uint32_t *)(APP_ADDR + 0u); }
    if (entry != 0) { *entry = *(volatile uint32_t *)(APP_ADDR + 4u); }
    return 1;
}

const char *FlashIf_ResultName(FlashIfResult r)
{
    switch (r)
    {
    case FLASHIF_OK:           return "ok";
    case FLASHIF_ERR_RANGE:    return "out of range";
    case FLASHIF_ERR_UNLOCK:   return "unlock failed";
    case FLASHIF_ERR_ERASE:    return "erase failed";
    case FLASHIF_ERR_PROGRAM:  return "program failed";
    case FLASHIF_ERR_VERIFY:   return "verify mismatch";
    default:                   return "?";
    }
}
