/**
 ******************************************************************************
 * @file    version.c
 * @brief   版本信息实现
 ******************************************************************************
 */
#include "version.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f4xx_hal.h"

static uint32_t s_fault_count = 0;

/* 串口输出（不用 printf 重定向，避免半主机）*/
extern UART_HandleTypeDef huart1;

static void app_print(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 1000);
}

uint32_t Version_Number(void)        { return FW_VERSION; }
const char *Version_String(void)     { return FW_VERSION_STR; }
void     Version_FaultInc(void)      { s_fault_count++; }
uint32_t Version_FaultCount(void)    { return s_fault_count; }

void Version_PrintBanner(void)
{
    char buf[128];

    snprintf(buf, sizeof(buf),
             "==============================================\r\n"
             " APP v%s   built %s %s\r\n"
             " sample period %u ms\r\n"
             "==============================================\r\n",
             FW_VERSION_STR, __DATE__, __TIME__, (unsigned)FW_SAMPLE_MS);
    app_print(buf);
}
