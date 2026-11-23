/**
 ******************************************************************************
 * @file    bsp_delay.c
 * @brief   基于 DWT 周期计数器的阻塞延时
 ******************************************************************************
 */
#include "bsp_delay.h"

#include "stm32f4xx.h"

/* DWT 相关寄存器（CMSIS 里 core_cm4.h 有定义，但不同版本名字略有差异，
 * 这里直接用地址访问，避免版本兼容问题）*/
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004u)
#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFCu)

#define DWT_CTRL_CYCCNTENA  (1u << 0)
#define DEMCR_TRCENA        (1u << 24)

void Delay_Init(void)
{
    /* 使能跟踪单元，再使能周期计数器 */
    CoreDebug_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT      = 0u;
    DWT_CTRL       |= DWT_CTRL_CYCCNTENA;
}

void Delay_ms(uint32_t n)
{
    uint32_t start;
    uint32_t ticks;

    if (n == 0u) { return; }

    /* SystemCoreClock = 180000000，/1000 = 180000 周期 / ms
     * n 最大约 23000 时 ticks 仍在 uint32 范围内，够用。
     * 超大延时会回绕，这里做分块处理更稳妥。 */
    while (n > 0u)
    {
        uint32_t chunk = (n > 4000u) ? 4000u : n;   /* 每次最多 4000 ms */

        ticks = chunk * (SystemCoreClock / 1000u);
        start = DWT_CYCCNT;

        /* 用无符号减法，天然处理 CYCCNT 回绕 */
        while ((uint32_t)(DWT_CYCCNT - start) < ticks)
        {
        }

        n -= chunk;
    }
}

void Delay_us(uint32_t n)
{
    uint32_t start;
    uint32_t ticks;

    if (n == 0u) { return; }

    ticks = n * (SystemCoreClock / 1000000u);
    start = DWT_CYCCNT;

    while ((uint32_t)(DWT_CYCCNT - start) < ticks)
    {
    }
}
