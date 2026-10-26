/**
 ******************************************************************************
 * @file    key.c
 * @brief   两键消抖扫描实现
 *
 * 说明：这两个引脚由本文件自己初始化（不依赖 CubeMX 里有没有配）。
 *       CubeMX 里再配一遍也不会冲突，配置一模一样。
 ******************************************************************************
 */
#include "key.h"

#define KEY_PRESSED_LEVEL   1    /* 内部下拉 + 按下接高电平 */
#define DEBOUNCE_TICKS      2    /* 连续 2 次（约 20 ms）都按下才算数 */

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       state;   /* 0=等按下  1=消抖计时  2=等松开 */
    uint8_t       cnt;
} key_t;

static key_t s_keys[2];

void Key_Init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLDOWN;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pin = KEY_UP_PIN;
    HAL_GPIO_Init(KEY_UP_PORT, &gi);

    gi.Pin = KEY_DOWN_PIN;
    HAL_GPIO_Init(KEY_DOWN_PORT, &gi);

    s_keys[0].port = KEY_UP_PORT;    s_keys[0].pin = KEY_UP_PIN;
    s_keys[0].state = 0;             s_keys[0].cnt = 0;
    s_keys[1].port = KEY_DOWN_PORT;  s_keys[1].pin = KEY_DOWN_PIN;
    s_keys[1].state = 0;             s_keys[1].cnt = 0;
}

/**
 * @brief  单个按键的三态机
 * @return 本次是否产生"按下"事件
 */
static key_event_t scan_one(key_t *k, key_event_t ev)
{
    uint8_t pressed = (HAL_GPIO_ReadPin(k->port, k->pin) == KEY_PRESSED_LEVEL) ? 1 : 0;

    switch (k->state)
    {
    case 0:                                  /* 等按下 */
        if (pressed)
        {
            k->state = 1;
            k->cnt   = 0;
        }
        break;

    case 1:                                  /* 消抖计时 */
        if (++k->cnt >= DEBOUNCE_TICKS)
        {
            if (pressed) { k->state = 2; return ev; }   /* 确认按下 → 产生事件 */
            else         { k->state = 0; }              /* 是抖动 → 丢弃 */
        }
        break;

    case 2:                                  /* 等松开：不产生事件，所以按住不重复触发 */
        if (!pressed) k->state = 0;
        break;

    default:
        k->state = 0;
        break;
    }

    return KEY_NONE;
}

key_event_t Key_Scan(void)
{
    key_event_t e1 = scan_one(&s_keys[0], KEY_UP_EVENT);
    key_event_t e2 = scan_one(&s_keys[1], KEY_DOWN_EVENT);

    return (e1 != KEY_NONE) ? e1 : e2;
}
