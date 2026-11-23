/**
 ******************************************************************************
 * @file    app_layout.c
 * @brief   分区与 APP 有效性判断实现（纯 C，不依赖硬件）
 ******************************************************************************
 */
#include "app_layout.h"

int App_IsMspValid(uint32_t msp, int allow_ccm)
{
    /* 空片是 0xFFFFFFFF，这个值不能当 MSP 用 */
    if (msp == 0xFFFFFFFFu || msp == 0u) { return 0; }

    /* MSP 必须是 4 字节对齐（栈指针低两位永远是 0）*/
    if ((msp & 0x3u) != 0u) { return 0; }

    if (msp >= SRAM_BASE_ADDR && msp <= SRAM_END_ADDR) { return 1; }

    /* F429IGT6 还有 64 KB CCM RAM @0x10000000（只能放数据，但栈可以）*/
    if (allow_ccm && msp > 0x10000000u && msp <= (0x10000000u + 64u * 1024u))
    {
        return 1;
    }

    return 0;
}

int App_IsEntryValid(uint32_t entry)
{
    uint32_t addr;

    if (entry == 0xFFFFFFFFu || entry == 0u) { return 0; }

    /* Cortex-M 的复位入口必须是 Thumb 地址：最低位 = 1。
     * 这是最容易漏掉的一条 —— 少了它，跳到偶数地址会直接 HardFault。 */
    if ((entry & 0x1u) == 0u) { return 0; }

    /* 去掉 Thumb 位后，真实地址必须落在 APP 区内 */
    addr = entry & ~0x1u;

    if (addr < APP_ADDR)        { return 0; }
    if (addr >= APP_END_ADDR)   { return 0; }

    return 1;
}

AppImgResult App_CheckVector(uint32_t msp, uint32_t entry)
{
    /* 先判"空"：空片两个值都是 0xFFFFFFFF */
    if (msp == 0xFFFFFFFFu || msp == 0u)   { return APP_IMG_ERR_EMPTY; }
    if (entry == 0xFFFFFFFFu || entry == 0u) { return APP_IMG_ERR_EMPTY; }

    if ((entry & 0x1u) == 0u)              { return APP_IMG_ERR_ENTRY_THUMB; }

    if (!App_IsMspValid(msp, 1))           { return APP_IMG_ERR_MSP; }
    if (!App_IsEntryValid(entry))          { return APP_IMG_ERR_ENTRY_RANGE; }

    /* 向量表要按 0x200 对齐（VTOR 的低 9 位保留为 0）*/
    if ((APP_ADDR & 0x1FFu) != 0u)         { return APP_IMG_ERR_ALIGN; }

    return APP_IMG_OK;
}

int App_IsRangeInApp(uint32_t addr, uint32_t len)
{
    uint32_t end;

    /* 零长度直接拒绝（任务书 P0-06 明确要求）*/
    if (len == 0u) { return 0; }

    /* 溢出检查：addr + len 不能回绕 */
    end = addr + len;
    if (end < addr) { return 0; }

    if (addr < APP_ADDR)      { return 0; }
    if (end > APP_END_ADDR)   { return 0; }

    return 1;
}

const char *App_ImgResultName(AppImgResult r)
{
    switch (r)
    {
    case APP_IMG_OK:              return "ok";
    case APP_IMG_ERR_MSP:         return "bad MSP";
    case APP_IMG_ERR_ENTRY_RANGE: return "entry out of range";
    case APP_IMG_ERR_ENTRY_THUMB: return "entry not thumb";
    case APP_IMG_ERR_EMPTY:       return "empty (never flashed)";
    case APP_IMG_ERR_ALIGN:       return "vector not aligned";
    default:                      return "?";
    }
}
