/**
 ******************************************************************************
 * @file    mpu6050.h
 * @brief   MPU6050 六轴传感器驱动（I2C 寄存器级，裸 HAL）
 *
 * 传感器概况：
 *   - 3 轴加速度（±2g / ±4g / ±8g / ±16g 可选）
 *   - 3 轴陀螺仪（±250 / ±500 / ±1000 / ±2000 °/s 可选）
 *   - I2C 从机地址：0x68（AD0 接地）或 0x69（AD0 接高）
 *   - 图纸上标 0xD0 = 0x68 << 1（HAL 要的就是这个 8 位形式）
 *
 * 使用示例：
 *     MPU6050_HandleTypeDef imu;
 *     if (MPU6050_Init(&imu, &hi2c1) == MPU6050_OK)
 *     {
 *         MPU6050_RawData_t d;
 *         MPU6050_ReadRaw(&imu, &d);
 *         printf("ax=%d ay=%d az=%d\r\n", d.ax, d.ay, d.az);
 *     }
 ******************************************************************************
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---------------- 返回值 ---------------- */
typedef enum
{
    MPU6050_OK = 0,
    MPU6050_ERR_WHOAMI,     /* 芯片不应答，或读回的 ID 不对 */
    MPU6050_ERR_I2C,        /* I2C 读写失败 */
    MPU6050_ERR_NOT_INIT
} MPU6050_Status;

/* ---------------- 寄存器地址（只列用到的） ---------------- */
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_ACCEL_XOUT_H    0x3B    /* 连续 14 字节：ax/ay/az/temp/gx/gy/gz */
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_WHO_AM_I        0x75

/* WHO_AM_I 的固定应答值 */
#define MPU6050_WHO_AM_I_VALUE      0x68

/* ---------------- 量程选择 ---------------- */
typedef enum
{
    MPU6050_ACCEL_2G = 0,       /* ±2g   -> 16384 LSB/g  */
    MPU6050_ACCEL_4G,           /* ±4g   -> 8192  LSB/g  */
    MPU6050_ACCEL_8G,           /* ±8g   -> 4096  LSB/g  */
    MPU6050_ACCEL_16G           /* ±16g  -> 2048  LSB/g  */
} MPU6050_AccelRange;

typedef enum
{
    MPU6050_GYRO_250 = 0,       /* ±250°/s  -> 131   LSB/(°/s) */
    MPU6050_GYRO_500,           /* ±500°/s  -> 65.5             */
    MPU6050_GYRO_1000,          /* ±1000°/s -> 32.8             */
    MPU6050_GYRO_2000           /* ±2000°/s -> 16.4             */
} MPU6050_GyroRange;

/* ---------------- 原始数据 ---------------- */
typedef struct
{
    int16_t ax;     /* 加速度，单位 LSB，正负由量程决定 */
    int16_t ay;
    int16_t az;
    int16_t gx;     /* 陀螺仪角速度，单位 LSB */
    int16_t gy;
    int16_t gz;
    int16_t temp;   /* 片上温度，换算见 MPU6050_RawToTemp() */
} MPU6050_RawData_t;

/* ---------------- 句柄 ---------------- */
typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint16_t           addr8;           /* 8 位形式的地址（0xD0 或 0xD2） */
    MPU6050_AccelRange accel_range;
    MPU6050_GyroRange  gyro_range;
    uint8_t            initialized;
    /* 静止校准得到的零偏（原始 LSB） */
    int16_t            gyro_bias[3];
} MPU6050_HandleTypeDef;

/* ---------------- 接口 ---------------- */

/**
 * @brief  初始化：验证 WHO_AM_I、唤醒、设置量程与采样率
 * @param  dev  句柄
 * @param  hi2c 已初始化的 I2C 句柄（如 &hi2c1）
 * @retval MPU6050_OK 成功；否则见 MPU6050_Status
 */
MPU6050_Status MPU6050_Init(MPU6050_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c);

/**
 * @brief  读 WHO_AM_I（0x75），正常应返回 0x68
 * @note   这是判断"芯片到底在不在"最可靠的手段
 */
MPU6050_Status MPU6050_ReadWhoAmI(MPU6050_HandleTypeDef *dev, uint8_t *id);

/**
 * @brief  读一次全部原始数据（一次 I2C 事务读 14 字节）
 */
MPU6050_Status MPU6050_ReadRaw(MPU6050_HandleTypeDef *dev, MPU6050_RawData_t *out);

/**
 * @brief  静止校准：连续采样 N 次求平均，记录陀螺仪零偏
 * @note   必须在【静止】状态下调用。上电后调用一次即可。
 * @param  samples 采样次数，建议 200
 */
MPU6050_Status MPU6050_CalibrateGyro(MPU6050_HandleTypeDef *dev, uint16_t samples);

/**
 * @brief  取"扣掉零偏"后的陀螺仪角速度（LSB）
 */
void MPU6050_GetGyroBiasCorrected(MPU6050_HandleTypeDef *dev,
                                  const MPU6050_RawData_t *raw,
                                  int16_t *gx, int16_t *gy, int16_t *gz);

/**
 * @brief  原始温度换算成 摄氏度
 */
float MPU6050_RawToTemp(int16_t raw_temp);

/**
 * @brief  加速度原始值换算成 g（重力倍数）
 */
float MPU6050_RawToG(MPU6050_HandleTypeDef *dev, int16_t raw, uint8_t axis);

#endif /* __MPU6050_H */
