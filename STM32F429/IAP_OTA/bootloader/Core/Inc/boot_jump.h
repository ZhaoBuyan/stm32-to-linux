/**
 ******************************************************************************
 * @file    boot_jump.h
 * @brief   Bootloader -> APP 跳转（**本项目最容易出错的一段**）
 *
 * 任务书 P0-08 要求：跳转前"完整处理使用过的外设/DMA/SysTick/NVIC/VTOR/MSP"。
 *
 * 为什么这么严：
 *   APP 和 Bootloader 是**两个独立编译的程序**，它们各自配置外设、各自有中断表。
 *   Bootloader 用过的外设如果没关干净，跳到 APP 后会出现：
 *
 *     ① 中断还挂在 Bootloader 的向量上 -> APP 里一触发就跳到已失效的代码 -> HardFault
 *     ② SysTick 还在按 Bootloader 的频率跑 -> APP 的 HAL 时基全乱
 *     ③ VTOR 还指向 Bootloader -> APP 的中断全部导错地方
 *     ④ MSP 还是 Bootloader 的栈 -> APP 一压栈就踩到旧数据
 *
 *   症状往往是"APP 能跑，但串口/定时器就是不通"，而且调试器看着一切正常。
 ******************************************************************************
 */
#ifndef __BOOT_JUMP_H
#define __BOOT_JUMP_H

#include <stdint.h>

/** 跳转到 APP。**正常情况下不会返回。**
 *
 *  @param app_addr  APP 起始地址（本项目 = 0x08020000）
 *
 *  调用前请先自己确认 APP 有效（App_CheckVector），
 *  这个函数只负责"清理上下文 + 跳"，不做有效性判断。
 */
void BootJump_ToApp(uint32_t app_addr);

/** 仅做清理，不跳转（调试用：可以先清理再回来验证寄存器）*/
void BootJump_Cleanup(void);

#endif /* __BOOT_JUMP_H */
