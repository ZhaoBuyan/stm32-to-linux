#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

/* 初始化：使能 DWT 周期计数器（微秒级计时用） */
void DWT_Init(void);

/* 读取一次温湿度
 * 返回值: 0 = 成功, 1 = 无响应, 2 = 校验失败
 * temp/humi: 输出参数（摄氏度 / 相对湿度）
 */
uint8_t DHT_Read(uint8_t *temp, uint8_t *humi);

#endif /* __DHT11_H */
