/**
 ******************************************************************************
 * @file    key.h
 * @brief   两个按键的消抖扫描（KEY_UP / KEY_DOWN）
 *
 * 复用自考核工程 key.c 的三态机：等按下 → 连续 2 次都按下才确认 → 等松开。
 * 在"等松开"状态里不发事件，所以**按住不放不会重复触发**。
 ******************************************************************************
 */
#ifndef __KEY_H
#define __KEY_H

#include "main.h"

/* 引脚定义（板载按键，按下为高电平） */
#define KEY_UP_PORT     GPIOA
#define KEY_UP_PIN      GPIO_PIN_0     /* PA0  */
#define KEY_DOWN_PORT   GPIOC
#define KEY_DOWN_PIN    GPIO_PIN_13    /* PC13 */

typedef enum
{
    KEY_NONE = 0,
    KEY_UP_EVENT,      /* KEY_UP 被按下一次 */
    KEY_DOWN_EVENT     /* KEY_DOWN 被按下一次 */
} key_event_t;

void        Key_Init(void);
key_event_t Key_Scan(void);   /* 约定：主循环里每 10~20 ms 调一次 */

#endif /* __KEY_H */
