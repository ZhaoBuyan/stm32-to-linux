/**
 * sim_hal.h —— 在 PC 上假装自己是 STM32 HAL
 *
 * 目的：不开板子也能验证 EE_WriteBuf 的分页算法是否正确。
 * 提供的假 HAL 会**如实模拟 AT24C02 的页内回绕**（这是真机最坑的地方）。
 */
#ifndef SIM_HAL_H
#define SIM_HAL_H

#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

typedef struct { int dummy; } I2C_HandleTypeDef;

#define I2C_MEMADD_SIZE_8BIT 1

extern I2C_HandleTypeDef hi2c1;

/* 模拟出来的 EEPROM 内容（256 字节），测试直接读它比对 */
extern uint8_t  g_ee_mem[256];
/* 统计：一共发生了几次页写（用来验证"确实分了页"） */
extern uint32_t g_ee_writes;
/* 统计：累计等待了多少 ms 写周期 */
extern uint32_t g_ee_delay_ms;

void HAL_Delay(uint32_t ms);

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t memAddr,
                                    uint16_t memSize, uint8_t *pData, uint16_t size, uint32_t timeout);

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t memAddr,
                                   uint16_t memSize, uint8_t *pData, uint16_t size, uint32_t timeout);

#endif /* SIM_HAL_H */
