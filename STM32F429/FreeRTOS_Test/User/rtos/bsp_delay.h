/**
 ******************************************************************************
 * @file    bsp_delay.h
 * @brief   阻塞延时（DWT 周期计数器实现）
 *
 * 为什么不用野火原来的 bsp_SysTick.c：
 *   FreeRTOS 要独占 SysTick 做系统节拍（xPortSysTickHandler）。
 *   原来的 Delay_ms 靠 SysTick 中断递减计数，会和内核抢同一个中断，
 *   结果是延时不准、任务调度紊乱。
 *
 * 为什么选 DWT 而不是定时器：
 *   - 不需要额外外设，不占中断
 *   - 在调度器启动**之前**也能用（LCD 初始化、外设上电时序都要延时）
 *   - 精度到 CPU 周期（180 MHz）
 *
 * ⚠️ 这是**阻塞**延时。在任务里请优先用 vTaskDelay()，
 *    否则会白白占着 CPU，其他任务拿不到时间片。
 *    只有"必须在调度器启动前"或"驱动内部的短延时（<几 ms）"才用它。
 ******************************************************************************
 */
#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include <stdint.h>

/** 使能 DWT 周期计数器。必须在任何 Delay_ms 之前调用一次。 */
void Delay_Init(void);

/** 阻塞延时 n 毫秒（忙等，不释放 CPU） */
void Delay_ms(uint32_t n);

/** 阻塞延时 n 微秒 */
void Delay_us(uint32_t n);

#endif /* __BSP_DELAY_H */
