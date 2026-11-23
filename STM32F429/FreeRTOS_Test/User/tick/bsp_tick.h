/**
 ******************************************************************************
 * @file    bsp_tick.h
 * @brief   TIM6 产生 1ms 节拍，给游戏当时间基准
 *
 * 为什么不直接用 SysTick：
 *   野火的 bsp_SysTick.c 只用 SysTick 做 Delay_ms 的阻塞延时，
 *   它内部的计数器是 static 的，外部拿不到。
 *   所以另开 TIM6 做自由运行的毫秒计数，互不干扰。
 ******************************************************************************
 */
#ifndef __BSP_TICK_H
#define __BSP_TICK_H

#include "stm32f4xx.h"

/* 启动 TIM6 的 1ms 中断 */
void Tick_Init(void);

/* 已走过的毫秒数（1ms 自增，溢出回绕无影响） */
uint32_t Tick_Get(void);

#endif /* __BSP_TICK_H */
