/**
 ******************************************************************************
 * @file    gps_uart.c
 * @brief   USART3 + 中断接收 + 环形缓冲（StdPeriph 库）
 *
 * 引脚（避开已占用的 LTDC / FMC / SPI5 / I2C1 / SWD）：
 *   PB10 = USART3_TX
 *   PB11 = USART3_RX
 ******************************************************************************
 */
#include "gps_uart.h"
#include "stm32f4xx.h"

/* ---------------- 环形缓冲 ---------------- */
#define GPS_RB_SIZE     1024            /* 必须是 2 的幂，才能用 & 取模 */
#define GPS_RB_MASK     (GPS_RB_SIZE - 1)

static volatile uint8_t  s_rb[GPS_RB_SIZE];
static volatile uint16_t s_head = 0;    /* 中断写 */
static volatile uint16_t s_tail = 0;    /* 主循环读 */
static volatile uint32_t s_total = 0;   /* 累计字节数 */
static volatile uint32_t s_dropped = 0;
static uint32_t          s_baud = 0;

/* ---------------- 初始化 ---------------- */
void GpsUart_Init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    s_baud = baud;

    /* 时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* PB10 = TX，PB11 = RX，复用功能 */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    gpio.GPIO_Pin   = GPIO_Pin_10 | GPIO_Pin_11;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;      /* RX 上拉：模块没接时不至于乱码 */
    GPIO_Init(GPIOB, &gpio);

    /* USART3：8-N-1，收发都开 */
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &usart);

    /* 开接收中断 */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    nvic.NVIC_IRQChannel                   = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART3, ENABLE);
}

uint32_t GpsUart_Baud(void)       { return s_baud; }
uint32_t GpsUart_TotalBytes(void) { return s_total; }

void GpsUart_Reset(void)
{
    s_head = 0;
    s_tail = 0;
    s_total = 0;
    s_dropped = 0;
}

/* ---------------- 中断侧：只做一件事 ---------------- */
void GpsUart_IsrPush(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_head + 1) & GPS_RB_MASK);

    s_total++;

    if (next == s_tail)
    {
        /* 缓冲满：丢弃新字节并计数。
         * 这里刻意不覆盖旧数据 —— 旧数据是完整句子的开头，更值钱。 */
        s_dropped++;
        return;
    }

    s_rb[s_head] = byte;
    s_head = next;
}

int GpsUart_ReadByte(void)
{
    if (s_tail == s_head) { return -1; }        /* 空 */

    {
        uint8_t b = s_rb[s_tail];
        s_tail = (uint16_t)((s_tail + 1) & GPS_RB_MASK);
        return (int)b;
    }
}
