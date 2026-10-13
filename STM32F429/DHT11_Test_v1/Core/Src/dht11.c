#include "dht11.h"

/* ==================================================================
 *  DHT11 驱动（单总线协议）
 *
 *  依赖：DWT 周期计数器（微秒级计时）
 *  引脚：DATA = PB0（由 CubeMX 的 User Label "DHT11" 定义）
 * ================================================================== */

#define DHT_PIN_NUM 0 /* PB0 → 引脚号 0（换引脚时这里要改） */
#define US_TICKS 180u /* 180MHz → 1us = 180 个周期 */

/* ---------- 使能 DWT 周期计数器（微秒计时的基础）---------- */
void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* 打开内核跟踪单元 */
    DWT->CYCCNT = 0;                                /* 计数器清零 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;            /* 使能周期计数 */
}

/* ---------- 把数据引脚切成【输出】（主机说话时用）---------- */
void DHT_SetOut(void) {
    DHT11_GPIO_Port->MODER = (DHT11_GPIO_Port->MODER & ~(3u << (DHT_PIN_NUM * 2))) | (1u << (DHT_PIN_NUM * 2)); /* MODER=01 → 输出 */
}

/* ---------- 把数据引脚切成【输入】（听从机说话时用）---------- */
void DHT_SetIn(void) {
    DHT11_GPIO_Port->MODER &= ~(3u << (DHT_PIN_NUM * 2)); /* MODER=00 → 输入 */
}

/* ---------- 测"引脚保持某个电平多久"（微秒），带超时 ----------
 *  返回值：持续时长(us)；超时返回 0xFFFFFFFF
 *  做法：  先等它【变成】level，再量它【保持】level 多久
 * --------------------------------------------------------------- */
uint32_t DHT_WaitLevel(uint32_t level, uint32_t timeout_us) {
    uint32_t max = timeout_us * US_TICKS; /* 超时值换算成"周期数" */
    uint32_t start;

    /* 第一步：等它变成 level */
    start = DWT->CYCCNT;
    while (((DHT11_GPIO_Port->IDR & DHT11_Pin) ? 1 : 0) != level) {
        if ((DWT->CYCCNT - start) > max) {
            return 0xFFFFFFFF; /* 超时 */
        }
    }

    /* 第二步：量它保持 level 多久 */
    start = DWT->CYCCNT;
    while (((DHT11_GPIO_Port->IDR & DHT11_Pin) ? 1 : 0) == level) {
        if ((DWT->CYCCNT - start) > max) {
            return 0xFFFFFFFF;
        }
    }
    return (DWT->CYCCNT - start) / US_TICKS; /* 周期数 → 微秒 */
}

/* ---------- 读 1 个 bit ----------
 *  每个 bit 的波形：低电平 50us（固定）+ 高电平（26us=0 / 70us=1）
 *  所以：测"高电平持续多久"，> 50us 就是 1
 * -------------------------------------------------- */
uint8_t DHT_ReadBit(void) {
    uint32_t t;

    DHT_WaitLevel(0, 100);     /* 等"50us 低电平段"结束 */
    t = DHT_WaitLevel(1, 100); /* 测"高电平"持续多久 */

    return (t > 50) ? 1 : 0; /* >50us 是 1，否则是 0 */
}

/* ---------- 读 1 个字节（8 个 bit）----------
 *  DHT11 是【高位先发】→ 每次左移一位，把新 bit 放到最低位
 * -------------------------------------------- */
uint8_t DHT_ReadByte(void) {
    uint8_t i, b = 0;

    for (i = 0; i < 8; i++) {
        b <<= 1;            /* 左移，腾出最低位 */
        b |= DHT_ReadBit(); /* 把新 bit 放进去 */
    }
    return b;
}

/* ---------- 完整读一次温湿度 ----------
 *  返回值：0 = 成功；1 = 无响应；2 = 校验失败
 *  输出：  *temp 温度(℃)、*humi 湿度(%RH)
 * -------------------------------------- */
uint8_t DHT_Read(uint8_t *temp, uint8_t *humi) {
    uint8_t i, d[5];

    /* ① 主机发起始信号：拉低 18ms，再释放 */
    DHT_SetOut();
    DHT11_GPIO_Port->BSRR = (DHT11_Pin << 16); /* 拉低 */
    HAL_Delay(18);
    DHT11_GPIO_Port->BSRR = DHT11_Pin; /* 释放（拉高）*/
    DHT_SetIn();

    /* ② 等从机响应：80us 低 + 80us 高 */
    if (DHT_WaitLevel(0, 100) == 0xFFFFFFFF)
        return 1;
    if (DHT_WaitLevel(1, 100) == 0xFFFFFFFF)
        return 1;

    /* ③ 读 5 个字节 */
    for (i = 0; i < 5; i++) {
        d[i] = DHT_ReadByte();
    }

    /* ④ 校验：前 4 字节之和的低 8 位 == 第 5 字节 */
    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4])
        return 2;

    /* ⑤ 输出（DHT11 小数部分固定为 0，只取整数）*/
    *humi = d[0]; /* 湿度整数 */
    *temp = d[2]; /* 温度整数 */
    return 0;
}
