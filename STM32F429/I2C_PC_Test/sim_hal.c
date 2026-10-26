/**
 * sim_hal.c —— 假 HAL 的实现：如实复刻 AT24C02 的页写行为
 *
 * 关键的一条（真机上就是这样，而且**不报错**）：
 *   一次页写最多落到 8 字节的同一页内，而且**页内地址会回绕**。
 *   例：从 0x06 开始一次写 4 字节 → 实际落到 0x06, 0x07, 0x00, 0x01
 *       后面两个把页开头的 0x00, 0x01 覆盖掉了。
 */
#include "sim_hal.h"
#include <string.h>

I2C_HandleTypeDef hi2c1;
uint8_t  g_ee_mem[256];
uint32_t g_ee_writes   = 0;
uint32_t g_ee_delay_ms = 0;

void HAL_Delay(uint32_t ms)
{
    g_ee_delay_ms += ms;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t memAddr,
                                    uint16_t memSize, uint8_t *pData, uint16_t size, uint32_t timeout)
{
    uint16_t i;
    uint8_t  pageBase, offset;

    (void)h; (void)dev; (void)memSize; (void)timeout;

    if (size == 0) return HAL_OK;

    g_ee_writes++;

    /* 硬件行为：整页的基址 = 地址的高 5 位；页内偏移在 0~7 之间回绕 */
    pageBase = (uint8_t)(memAddr & 0xF8);
    offset   = (uint8_t)(memAddr & 0x07);

    for (i = 0; i < size; i++)
    {
        g_ee_mem[pageBase + ((offset + i) & 0x07)] = pData[i];
    }

    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t memAddr,
                                   uint16_t memSize, uint8_t *pData, uint16_t size, uint32_t timeout)
{
    uint16_t i;

    (void)h; (void)dev; (void)memSize; (void)timeout;

    /* 读是顺序的，地址自动递增并回绕 —— 不需要分页 */
    for (i = 0; i < size; i++)
    {
        pData[i] = g_ee_mem[(uint8_t)(memAddr + i)];
    }

    return HAL_OK;
}
