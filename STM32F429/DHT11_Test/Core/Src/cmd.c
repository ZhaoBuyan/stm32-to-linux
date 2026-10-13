#include "cmd.h"
#include "led.h"
#include <stdio.h>
#include <string.h>

/* 串口句柄在 main.c 里定义，这里只引用 */
extern UART_HandleTypeDef huart1;

/* 命令最多 32 字节（含 '@' 和 '\n'），多留几个字节防越界 */
#define CMD_BUF_SIZE    40
#define CMD_MAX_CHARS   31     /* 除结束符 '\n' 外，最多存 31 个字符 */
#define TIMEOUT_TICKS   200    /* 200 * 10ms = 2s */
#define BLINK_TICKS     50     /* 50 * 10ms = 500ms */
#define REPORT_TICKS    200    /* 2s */

/* ---- 接收状态机 ---- */
#define ST_IDLE  0
#define ST_RECV  1

static char     s_buf[CMD_BUF_SIZE];
static uint16_t s_len;         /* 已存字符数 */
static uint8_t  s_state;       /* ST_IDLE / ST_RECV */
static uint16_t s_timeout;     /* 接收计时（单位 10ms，从 '@' 开始） */
static uint8_t  s_overflow;    /* 本命令是否超长 */
static uint8_t  s_line_ready;  /* 收到 '\n'，等主循环处理 */

/* ---- 闪烁 / 上报 ---- */
static uint8_t  s_blink, s_blink_cnt;
static uint8_t  s_report, s_report_cnt;

/* ---- 最近一次环境数据 ---- */
static uint8_t  s_env_t, s_env_h, s_env_valid;

static void send_str(const char *s) {
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
}

void Cmd_Init(void) {
    s_len = 0; s_state = ST_IDLE; s_timeout = 0;
    s_overflow = 0; s_line_ready = 0;
    s_blink = 0; s_blink_cnt = 0;
    s_report = 0; s_report_cnt = 0;
    s_env_t = 0; s_env_h = 0; s_env_valid = 0;
}

void Cmd_SetEnv(uint8_t temp, uint8_t humi, uint8_t valid) {
    s_env_t = temp; s_env_h = humi; s_env_valid = valid;
}

void Cmd_ExitBlink(void) {
    s_blink = 0;
}

/* 把 "@xxx" 解析并执行（在主循环里调用） */
static void exec_cmd(const char *cmd) {
    char out[48];

    if (strcmp(cmd, "@LED=1") == 0) {
        s_blink = 0;                       /* 有效 LED 命令退出闪烁 */
        LED_On();
        send_str("OK,LED=1\r\n");
    } else if (strcmp(cmd, "@LED=0") == 0) {
        s_blink = 0;
        LED_Off();
        send_str("OK,LED=0\r\n");
    } else if (strcmp(cmd, "@LED?") == 0) {
        sprintf(out, "LED=%u\r\n", (unsigned)LED_GetState());
        send_str(out);
    } else if (strcmp(cmd, "@ENV?") == 0) {
        if (s_env_valid) {
            sprintf(out, "ENV,T=%u,H=%u\r\n", (unsigned)s_env_t, (unsigned)s_env_h);
            send_str(out);
        } else {
            send_str("ERR,SENSOR\r\n");
        }
    } else if (strcmp(cmd, "@BLINK=1") == 0) {
        s_blink = 1; s_blink_cnt = 0;
        send_str("OK,BLINK=1\r\n");
    } else if (strcmp(cmd, "@BLINK=0") == 0) {
        s_blink = 0; LED_Off();
        send_str("OK,BLINK=0\r\n");
    } else if (strcmp(cmd, "@REPORT=1") == 0) {
        s_report = 1; s_report_cnt = 0;
        send_str("OK,REPORT=1\r\n");
    } else if (strcmp(cmd, "@REPORT=0") == 0) {
        s_report = 0;
        send_str("OK,REPORT=0\r\n");
    } else {
        send_str("ERR,CMD\r\n");
    }
}

/* 主循环里处理"一行已到齐" */
static void handle_line(void) {
    if (s_overflow) {
        send_str("ERR,LENGTH\r\n");        /* 超长：只报一次，随后恢复 */
    } else {
        s_buf[s_len] = '\0';
        exec_cmd(s_buf);
    }
    s_len = 0; s_state = ST_IDLE; s_timeout = 0; s_overflow = 0;
}

/* ==== 中断里调用：只收字节、只改状态，绝不发串口 ==== */
void Cmd_FeedByte(uint8_t b) {
    if (s_state == ST_IDLE) {
        if (b == '@') {                    /* 空闲时只认 '@' */
            s_state = ST_RECV;
            s_timeout = 0;
            s_overflow = 0;
            s_buf[0] = '@';
            s_len = 1;
        }
        return;                            /* 其他无关字符忽略 */
    }

    /* 接收中 */
    if (b == '\n') {                       /* 结束符 -> 交给主循环处理 */
        s_line_ready = 1;
        s_state = ST_IDLE;                 /* 立刻停止接收，等主循环处理 */
    } else if (b == '\r') {                /* 忽略 '\r'，且不重置计时 */
        /* nothing */
    } else if (b == '@') {                 /* 又见 '@'：丢弃旧命令，重开 */
        s_timeout = 0;
        s_overflow = 0;
        s_buf[0] = '@';
        s_len = 1;
    } else {
        if (s_len < CMD_MAX_CHARS) {
            s_buf[s_len++] = (char)b;
        } else {
            s_overflow = 1;                /* 超长：置标志，余下字符丢弃 */
        }
    }
}

/* ==== 主循环每 10ms 调用：处理命令、超时、闪烁、上报 ==== */
void Cmd_Tick10ms(void) {
    /* 1) 有整行命令：先处理（所有响应都在这里发） */
    if (s_line_ready) {
        s_line_ready = 0;
        handle_line();
    }

    /* 2) 接收超时：从收到 '@' 起满 2s 没等到 '\n' */
    if (s_state == ST_RECV) {
        if (++s_timeout >= TIMEOUT_TICKS) {
            send_str("ERR,TIMEOUT\r\n");
            s_len = 0; s_state = ST_IDLE; s_timeout = 0; s_overflow = 0;
        }
    }

    /* 3) 闪烁：每 500ms 翻转一次 */
    if (s_blink) {
        if (++s_blink_cnt >= BLINK_TICKS) {
            s_blink_cnt = 0;
            LED_Toggle();
        }
    }

    /* 4) 周期上报：每 2s 一次 */
    if (s_report) {
        if (++s_report_cnt >= REPORT_TICKS) {
            char out[48];
            s_report_cnt = 0;
            if (s_env_valid) {
                sprintf(out, "ENV,T=%u,H=%u\r\n", (unsigned)s_env_t, (unsigned)s_env_h);
                send_str(out);
            } else {
                send_str("ERR,SENSOR\r\n");
            }
        }
    }
}
