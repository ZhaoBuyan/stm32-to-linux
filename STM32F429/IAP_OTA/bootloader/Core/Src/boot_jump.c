/**
 ******************************************************************************
 * @file    boot_jump.c
 * @brief   Bootloader -> APP 跳转实现
 *
 * 顺序很重要，按下面这个来（少一步就可能出玄学 bug）：
 *
 *   1. 关全局中断（PRIMASK=1）—— 后面所有清理动作不能被中断打断
 *   2. 关 SysTick                —— 否则它在 APP 里还按旧频率跑
 *   3. 关掉 Bootloader 用过的外设 —— 本项目是 USART1 和 GPIO
 *   4. 清 NVIC 所有使能位和挂起位 —— 防止跳过去立刻触发一个旧中断
 *   5. 设 VTOR = APP 起始地址
 *   6. 从 APP 首 8 字节取 MSP 和 Reset_Handler
 *   7. 设 MSP
 *   8. 开全局中断
 *   9. 跳到 Reset_Handler（不返回）
 ******************************************************************************
 */
#include "boot_jump.h"

#include "main.h"
#include "stm32f4xx_hal.h"

/* 用来指向 APP 的复位入口。必须带 __asm 跳转或用函数指针，
 * 直接 goto 是不行的（跨工程）。 */
typedef void (*AppEntryFn)(void);

void BootJump_Cleanup(void)
{
    /* ---------- 1. 关全局中断 ---------- */
    __disable_irq();

    /* ---------- 2. 关 SysTick ----------
     * 只清 CTRL 的 ENABLE 位；LOAD/VAL 留着无所谓，
     * 因为 APP 的 HAL_Init 会重新配置。 */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* ---------- 3. 关掉 Bootloader 用过的外设 ----------
     * 本项目只用了 USART1（升级通道）和 GPIO（维护键、LED）。
     * 注意：DMA 本项目没用到；如果以后加了 DMA，必须在这里 DeInit。 */
    __HAL_RCC_USART1_CLK_DISABLE();

    /* 让 USART1 的引脚回到模拟态，避免残留在 AF 模式影响 APP 的引脚配置 */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);

    /* ---------- 4. 清 NVIC ----------
     * 把 0..81 号中断的使能位和挂起位全部清掉（F429 有 82 个可屏蔽中断）。
     * 不清的话，Bootloader 里使能过但没触发的中断，跳到 APP 后会立刻执行，
     * 而此时向量表已经变了 —— 典型表现是跳过去瞬间 HardFault。 */
    {
        uint32_t i;

        for (i = 0; i < 8u; i++)
        {
            NVIC->ICER[i] = 0xFFFFFFFFu;   /* 清使能 */
            NVIC->ICPR[i] = 0xFFFFFFFFu;   /* 清挂起 */
        }
    }

    /* 清掉 SCB 里可能残留的异常状态 */
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
}

void BootJump_ToApp(uint32_t app_addr)
{
    uint32_t   msp;
    uint32_t   entry;
    AppEntryFn jump;

    /* 从 APP 首 8 字节取：
     *   [0] 初始 MSP
     *   [1] Reset_Handler（Thumb 地址，最低位为 1）*/
    msp   = *(volatile uint32_t *)(app_addr + 0u);
    entry = *(volatile uint32_t *)(app_addr + 4u);

    /* ---------- 5~7. 清理 + 设 VTOR + 设 MSP ---------- */
    BootJump_Cleanup();

    SCB->VTOR = app_addr;

    __set_MSP(msp);

    /* ---------- 8. 开全局中断 ---------- */
    __enable_irq();

    /* ---------- 9. 跳 ---------- */
    jump = (AppEntryFn)entry;
    jump();

    /* 正常不会走到这里；如果走到了说明 APP 的 Reset_Handler 返回了，
     * 那是 APP 本身的问题 */
    for (;;) { }
}
