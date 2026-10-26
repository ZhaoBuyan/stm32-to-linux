/**
 * ee_impl.c —— EE_WriteBuf 的分页实现（PC 验证版）
 *
 * 上板时把下面这个函数**原样**搬到 eeprom.c 里。
 * PC 和真机的唯一区别是 HAL 那一层（这里用的是 sim_hal.c 的模拟实现）。
 */
#include "sim_hal.h"

#define AT24C02_ADDR    0xA0
#define AT24C02_PAGE    8
#define AT24C02_TWR_MS  5

/**
 * @brief  按 8 字节分页写入
 *
 * 思路：每次只写「当前页还剩下的空位」那么多，写完等写周期，再推进。
 *   room = 8 - (地址 % 8)   → 本页还剩几格
 *   n    = min(剩余长度, room)
 */
HAL_StatusTypeDef EE_WriteBuf(uint8_t memAddr, const uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;
    uint16_t room;
    uint16_t n;

    while (len > 0)
    {
        room = (uint16_t)(AT24C02_PAGE - (memAddr % AT24C02_PAGE));
        n    = (len < room) ? len : room;

        st = HAL_I2C_Mem_Write(&hi2c1, AT24C02_ADDR, memAddr,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, n, 100);
        if (st != HAL_OK)
        {
            return st;
        }

        HAL_Delay(AT24C02_TWR_MS);        /* 每写完一页都要等 tWR */

        memAddr = (uint8_t)(memAddr + n); /* 地址递增（0xFF 后自动回绕到 0x00）*/
        buf    += n;
        len    -= n;
    }

    return HAL_OK;
}
