/**
 ******************************************************************************
 * @file    app.h
 * @brief   业务层：3 组系统参数的加载 / 保存 / 显示 / 按键交互
 *
 * 分工：这一层只调 EE_xxx 和 OLED_xxx，不碰 HAL、不碰寄存器。
 *       这就是作业要求的"驱动与业务逻辑分离"。
 ******************************************************************************
 */
#ifndef __APP_H
#define __APP_H

#include "main.h"

/* 固件版本号：高字节主版本，低字节次版本 → 0x0100 = v1.00 */
#define APP_FW_VERSION      0x0100u

/* 出厂默认值 */
#define APP_DEFAULT_DEV_ID  0x0001u
#define APP_DEFAULT_FW_VER  APP_FW_VERSION

/* EEPROM 里数据有效的标志字节 */
#define APP_MAGIC           0xA5u

typedef struct
{
    uint16_t devId;      /* 设备 ID      */
    uint32_t runCount;   /* 运行计数     */
    uint16_t fwVer;      /* 固件版本号   */
} sys_param_t;

/* 上电调用：从 EEPROM 读参数；数据无效则恢复出厂默认并写回 */
void APP_Init(void);

/* 按键处理 */
void APP_KeyUp(void);      /* 运行计数 +1 → 存 EEPROM → 刷新显示 */
void APP_KeyDown(void);    /* 恢复出厂默认 → 存 EEPROM → 刷新显示 */

/* 重画整屏 */
void APP_Display(void);

/* 主循环每轮调用：只在参数变化时重画（I2C 刷屏很慢，别每轮都刷） */
void APP_Task(void);

/* 读当前参数（只读指针，别在外面改） */
const sys_param_t *APP_GetParam(void);

#endif /* __APP_H */
