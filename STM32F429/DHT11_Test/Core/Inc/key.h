#ifndef __KEY_H
#define __KEY_H

#include "main.h"

/* 按键事件：一次"按下"产生一次事件（持续按住不重复触发） */
typedef enum {
    KEY_NONE = 0,
    KEY1_PRESS,   /* PA0  按下 */
    KEY2_PRESS    /* PC13 按下 */
} key_event_t;

void        Key_Init(void);
key_event_t Key_Scan(void);   /* 约定：每 10ms 调用一次 */

#endif /* __KEY_H */
