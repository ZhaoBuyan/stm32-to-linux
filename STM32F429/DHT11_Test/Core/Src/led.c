#include "led.h"

#define LED_PIN GPIO_PIN_10
#define LED_PORT GPIOH

void LED_On(void) {
    LED_PORT->BSRR = (uint32_t)LED_PIN << 16; // 输出低，点亮
}

void LED_Off(void) {
    LED_PORT->BSRR = LED_PIN; // 输出高，熄灭
}

void LED_Toggle(void) {
    LED_PORT->ODR ^= LED_PIN; // 翻转 ODR 对应位
}

uint8_t LED_GetState(void) {
    return (LED_PORT->ODR & LED_PIN) ? 0 : 1; // 0 表示灯灭，1 表示灯亮
}
