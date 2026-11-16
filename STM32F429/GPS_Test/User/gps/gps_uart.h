/**
 ******************************************************************************
 * @file    gps_uart.h
 * @brief   GPS 模块的串口接收（USART3 + 中断 + 环形缓冲）
 *
 * 为什么用中断 + 环形缓冲，而不是主循环里轮询：
 *   GPS 以 9600~115200 连续吐 NMEA 语句，一行几十个字节。
 *   如果主循环里一边 printf 一边轮询，printf 稍慢就丢字节，
 *   而 NMEA 丢一个字节整行校验就废了。
 *
 *   所以：中断里只做一件事 —— 把一个字节塞进环形缓冲；
 *        主循环慢慢取、慢慢打印、慢慢解析。
 *   这就是"中断前后台"的最小实践。
 *
 * 硬件连接（USART3）：
 *   GPS TX  → MCU PB11 (USART3_RX)
 *   GPS RX  → MCU PB10 (USART3_TX)   ← 只收数据时不接也行
 *   VCC / GND 按模块标注（多数模块支持 3.3~5V）
 ******************************************************************************
 */
#ifndef __GPS_UART_H
#define __GPS_UART_H

#include <stdint.h>

/** 初始化 USART3（PB10 = TX，PB11 = RX），并开接收中断
 *  @param baud  波特率。常见：9600（NEO-6M 默认）、115200、38400、4800
 */
void Gps_Init(uint32_t baud);

/** 当前波特率（供显示） */
uint32_t Gps_Baud(void);

/** 从环形缓冲取一个字节。返回 -1 表示暂时没有数据（不阻塞） */
int Gps_ReadByte(void);

/** 累计收到多少字节（用于判断"到底有没有数据"） */
uint32_t Gps_TotalBytes(void);

/** 清空计数与缓冲（换波特率时用） */
void Gps_Reset(void);

/** 中断服务函数要用：把一个字节放进缓冲。由 USART3_IRQHandler 调用 */
void Gps_IsrPush(uint8_t byte);

#endif /* __GPS_UART_H */
