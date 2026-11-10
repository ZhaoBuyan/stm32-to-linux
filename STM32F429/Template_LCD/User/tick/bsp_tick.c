/**
 ******************************************************************************
 * @file    bsp_tick.c
 * @brief   TIM6 1ms 节拍
 *
 * 参数（按 90MHz 的 APB1 定时器时钟算）：
 *     90,000,000 / (8999+1) = 10,000 Hz
 *     10,000     / (9+1)    = 1,000 Hz  ->  1ms
 *
 * 注意：中断里只做 ++，不干别的。
 ******************************************************************************
 */
#include "bsp_tick.h"

static volatile uint32_t s_ms = 0;

void Tick_Init(void)
{
    TIM_TimeBaseInitTypeDef  tb;
    NVIC_InitTypeDef         nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    tb.TIM_Prescaler         = 8999;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = 9;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM6, &tb);

    TIM_ClearITPendingBit(TIM6, TIM_IT_Update);

    nvic.NVIC_IRQChannel                   = TIM6_DAC_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
}

uint32_t Tick_Get(void)
{
    return s_ms;
}

/* TIM6 和 DAC 共用一个中断向量 */
void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
        s_ms++;
    }
}
