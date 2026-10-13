#ifndef __CMD_H
#define __CMD_H

#include "main.h"

/* 串口命令层：负责"收字节 -> 组命令 -> 解析 -> 回响应"，
 * 以及 2 秒接收超时、闪烁、周期上报。
 *
 * 约定：
 *   - Cmd_FeedByte()  在 UART 接收中断里调用（只收字节，不发串口）
 *   - Cmd_Tick10ms()  在主循环里每 10ms 调用一次（所有响应都在这里发）
 */
void Cmd_Init(void);
void Cmd_FeedByte(uint8_t b);          /* 收到 1 个字节 */
void Cmd_Tick10ms(void);               /* 每 10ms 一次 */
void Cmd_SetEnv(uint8_t temp, uint8_t humi, uint8_t valid); /* 更新最近一次环境数据 */
void Cmd_ExitBlink(void);              /* 按键操作时调用：退出闪烁 */

#endif /* __CMD_H */
