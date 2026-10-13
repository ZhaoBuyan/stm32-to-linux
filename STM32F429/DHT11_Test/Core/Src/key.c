#include "key.h"

/* ---- 两个按键的引脚 ----
 * 按下时引脚读到 1（内部下拉输入）。
 * 若实测相反（某些板子按键接地 = 按下为低），把 KEY_PRESSED_LEVEL 改成 0 即可。
 */
#define KEY1_PORT  GPIOA
#define KEY1_PIN   GPIO_PIN_0
#define KEY2_PORT  GPIOC
#define KEY2_PIN   GPIO_PIN_13

#define KEY_PRESSED_LEVEL   1     /* 按下时读到的高/低 */
#define DEBOUNCE_TICKS      2     /* 连续 2 次(2*10ms=20ms)都按下才确认 */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       state;   /* 0=等按下  1=消抖计时  2=等松开 */
    uint8_t       cnt;
} key_t;

static key_t s_keys[2];

void Key_Init(void) {
    s_keys[0].port = KEY1_PORT; s_keys[0].pin = KEY1_PIN;
    s_keys[0].state = 0;        s_keys[0].cnt = 0;
    s_keys[1].port = KEY2_PORT; s_keys[1].pin = KEY2_PIN;
    s_keys[1].state = 0;        s_keys[1].cnt = 0;
}

/* 单个按键的状态机：返回本次是否产生"按下"事件 */
static key_event_t scan_one(key_t *k, key_event_t ev) {
    uint8_t pressed = (HAL_GPIO_ReadPin(k->port, k->pin) == KEY_PRESSED_LEVEL) ? 1 : 0;

    switch (k->state) {
    case 0:                       /* 等按下 */
        if (pressed) { k->state = 1; k->cnt = 0; }
        break;

    case 1:                       /* 消抖计时 */
        if (++k->cnt >= DEBOUNCE_TICKS) {
            if (pressed) { k->state = 2; return ev; }   /* 确认按下 -> 产生事件 */
            else         { k->state = 0; }              /* 是抖动 -> 丢弃 */
        }
        break;

    case 2:                       /* 等松开 */
        if (!pressed) k->state = 0;
        break;
    }
    return KEY_NONE;
}

key_event_t Key_Scan(void) {
    key_event_t e1 = scan_one(&s_keys[0], KEY1_PRESS);
    key_event_t e2 = scan_one(&s_keys[1], KEY2_PRESS);
    return (e1 != KEY_NONE) ? e1 : e2;
}
