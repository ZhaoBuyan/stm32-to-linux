/**
 ******************************************************************************
 * @file    mpu6050_bb.h
 * @brief   MPU6050 驱动（跑在野火的【软件模拟 I2C】上，不依赖 HAL）
 *
 * 为什么用软件 I2C：
 *   野火这版 LCD 工程的 GT911 触摸就是用 PB6/PB7 做软件 I2C 的，
 *   而 MPU6050 正好也在这两根线上。复用同一套 i2c_Start/SendByte/ReadByte，
 *   既不用配硬件 I2C 外设，也不会和触摸抢引脚。
 *
 * 前提：调用本驱动前必须先执行 I2C_Touch_Init() 或 GTP_Init_Panel()，
 *       把 PB6/PB7 配成软件 I2C 的推挽输出。
 ******************************************************************************
 */
#ifndef __MPU6050_BB_H
#define __MPU6050_BB_H

#include "stm32f4xx.h"

/* 从机地址（AD0 接地 = 0x68） */
#define MPU_ADDR7           0x68
#define MPU_ADDR8           (MPU_ADDR7 << 1)        /* 0xD0 */

/* 返回值 */
typedef enum
{
    MPU_OK = 0,
    MPU_ERR_WHOAMI,
    MPU_ERR_I2C,
    MPU_ERR_NOT_INIT
} MPU_Status;

/* 原始数据 */
typedef struct
{
    int16_t ax, ay, az;     /* 加速度 LSB（±2g 时 16384 = 1g） */
    int16_t gx, gy, gz;     /* 陀螺仪 LSB（±250°/s 时 131 = 1°/s） */
    int16_t temp;           /* 温度 LSB，换算见 MPU_RawToTemp */
} MPU_Raw;

typedef struct
{
    uint8_t initialized;
    int16_t gyro_bias[3];
} MPU_Dev;

/* 初始化：验证 WHO_AM_I、唤醒、设量程与采样率 */
MPU_Status MPU_Init(MPU_Dev *dev);

/* 只读 WHO_AM_I（0x75），正常返回 0x68 */
MPU_Status MPU_ReadWhoAmI(uint8_t *id);

/* 一次读 14 字节（加速度/温度/陀螺） */
MPU_Status MPU_ReadRaw(MPU_Dev *dev, MPU_Raw *out);

/* 静止校准陀螺零偏，调用时板子必须放平不动 */
MPU_Status MPU_CalibrateGyro(MPU_Dev *dev, uint16_t samples);

/* 扣掉零偏后的角速度 */
void MPU_GetGyroCorrected(MPU_Dev *dev, const MPU_Raw *raw,
                          int16_t *gx, int16_t *gy, int16_t *gz);

/* 温度换算成摄氏度 */
float MPU_RawToTemp(int16_t raw_temp);

#endif /* __MPU6050_BB_H */
