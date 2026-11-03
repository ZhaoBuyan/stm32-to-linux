/**
 ******************************************************************************
 * @file    mpu6050.c
 * @brief   MPU6050 驱动实现
 *
 * 关键点：
 *   1. HAL 的 I2C 接口要【8 位形式】地址 —— 0x68 << 1 = 0xD0
 *   2. 上电后芯片处于 sleep 模式，必须先清 PWR_MGMT_1 的 SLEEP 位
 *   3. 14 个数据寄存器是连续的，一次批量读比读 7 次快得多
 ******************************************************************************
 */
#include "mpu6050.h"
#include <string.h>

#define MPU6050_ADDR7        0x68u
#define MPU6050_REG_BLOCK    14u        /* ax,ay,az,temp,gx,gy,gz = 7 x 2 字节 */

/* ---------------- 内部小工具 ---------------- */

static MPU6050_Status wr_reg(MPU6050_HandleTypeDef *dev, uint8_t reg, uint8_t val)
{
    if (HAL_I2C_Mem_Write(dev->hi2c, dev->addr8, reg,
                          I2C_MEMADD_SIZE_8BIT, &val, 1, 100) != HAL_OK)
    {
        return MPU6050_ERR_I2C;
    }
    return MPU6050_OK;
}

static MPU6050_Status rd_regs(MPU6050_HandleTypeDef *dev, uint8_t reg,
                              uint8_t *buf, uint16_t len)
{
    if (HAL_I2C_Mem_Read(dev->hi2c, dev->addr8, reg,
                         I2C_MEMADD_SIZE_8BIT, buf, len, 100) != HAL_OK)
    {
        return MPU6050_ERR_I2C;
    }
    return MPU6050_OK;
}

/* ---------------- 对外接口 ---------------- */

MPU6050_Status MPU6050_ReadWhoAmI(MPU6050_HandleTypeDef *dev, uint8_t *id)
{
    uint8_t v = 0;

    if (dev == NULL || id == NULL) { return MPU6050_ERR_I2C; }

    if (rd_regs(dev, MPU6050_REG_WHO_AM_I, &v, 1) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }
    *id = v;

    return (v == MPU6050_WHO_AM_I_VALUE) ? MPU6050_OK : MPU6050_ERR_WHOAMI;
}

MPU6050_Status MPU6050_Init(MPU6050_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0;

    if (dev == NULL || hi2c == NULL) { return MPU6050_ERR_I2C; }

    memset(dev, 0, sizeof(*dev));
    dev->hi2c  = hi2c;
    dev->addr8 = (uint16_t)(MPU6050_ADDR7 << 1);     /* 0xD0 */
    dev->accel_range = MPU6050_ACCEL_2G;
    dev->gyro_range  = MPU6050_GYRO_250;

    /* ① 先确认芯片在不在 —— WHO_AM_I 是唯一可靠的身份证明 */
    if (MPU6050_ReadWhoAmI(dev, &id) != MPU6050_OK)
    {
        return MPU6050_ERR_WHOAMI;
    }

    /* ② 唤醒：清 SLEEP 位；时钟源选 X 轴陀螺 PLL（比内部 8MHz RC 稳） */
    if (wr_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x01) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }
    HAL_Delay(50);                                   /* 唤醒要给它一点时间 */

    /* ③ 采样率分频：1kHz / (1 + 4) = 200 Hz */
    if (wr_reg(dev, MPU6050_REG_SMPLRT_DIV, 4) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }

    /* ④ 数字低通滤波：DLPF_CFG = 3 → 约 44Hz 带宽，能压掉抖动 */
    if (wr_reg(dev, MPU6050_REG_CONFIG, 0x03) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }

    /* ⑤ 陀螺仪量程 ±250°/s，加速度 ±2g */
    if (wr_reg(dev, MPU6050_REG_GYRO_CONFIG,
               (uint8_t)(dev->gyro_range << 3)) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }
    if (wr_reg(dev, MPU6050_REG_ACCEL_CONFIG,
               (uint8_t)(dev->accel_range << 3)) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }

    dev->initialized = 1;
    return MPU6050_OK;
}

MPU6050_Status MPU6050_ReadRaw(MPU6050_HandleTypeDef *dev, MPU6050_RawData_t *out)
{
    uint8_t b[MPU6050_REG_BLOCK];

    if (dev == NULL || out == NULL || !dev->initialized)
    {
        return MPU6050_ERR_NOT_INIT;
    }

    /* 一次事务读 14 字节：从 ACCEL_XOUT_H 开始是连续的 */
    if (rd_regs(dev, MPU6050_REG_ACCEL_XOUT_H, b, MPU6050_REG_BLOCK) != MPU6050_OK)
    {
        return MPU6050_ERR_I2C;
    }

    /* 寄存器是【高字节在前】，拼成 int16_t */
    out->ax   = (int16_t)((b[0]  << 8) | b[1]);
    out->ay   = (int16_t)((b[2]  << 8) | b[3]);
    out->az   = (int16_t)((b[4]  << 8) | b[5]);
    out->temp = (int16_t)((b[6]  << 8) | b[7]);
    out->gx   = (int16_t)((b[8]  << 8) | b[9]);
    out->gy   = (int16_t)((b[10] << 8) | b[11]);
    out->gz   = (int16_t)((b[12] << 8) | b[13]);

    return MPU6050_OK;
}

MPU6050_Status MPU6050_CalibrateGyro(MPU6050_HandleTypeDef *dev, uint16_t samples)
{
    int32_t sum[3] = { 0, 0, 0 };
    uint16_t i;
    MPU6050_RawData_t d;

    if (dev == NULL || !dev->initialized) { return MPU6050_ERR_NOT_INIT; }
    if (samples == 0) { samples = 1; }

    for (i = 0; i < samples; i++)
    {
        if (MPU6050_ReadRaw(dev, &d) != MPU6050_OK) { return MPU6050_ERR_I2C; }
        sum[0] += d.gx;
        sum[1] += d.gy;
        sum[2] += d.gz;
        HAL_Delay(5);
    }

    dev->gyro_bias[0] = (int16_t)(sum[0] / (int32_t)samples);
    dev->gyro_bias[1] = (int16_t)(sum[1] / (int32_t)samples);
    dev->gyro_bias[2] = (int16_t)(sum[2] / (int32_t)samples);

    return MPU6050_OK;
}

void MPU6050_GetGyroBiasCorrected(MPU6050_HandleTypeDef *dev,
                                  const MPU6050_RawData_t *raw,
                                  int16_t *gx, int16_t *gy, int16_t *gz)
{
    if (dev == NULL || raw == NULL) { return; }

    if (gx) { *gx = (int16_t)(raw->gx - dev->gyro_bias[0]); }
    if (gy) { *gy = (int16_t)(raw->gy - dev->gyro_bias[1]); }
    if (gz) { *gz = (int16_t)(raw->gz - dev->gyro_bias[2]); }
}

float MPU6050_RawToTemp(int16_t raw_temp)
{
    /* 数据手册：Temp(°C) = raw/340 + 36.53 */
    return ((float)raw_temp / 340.0f) + 36.53f;
}

float MPU6050_RawToG(MPU6050_HandleTypeDef *dev, int16_t raw, uint8_t axis)
{
    float lsb_per_g;

    (void)axis;     /* 三个轴的换算系数相同，保留参数是为了将来做轴校正 */

    switch (dev->accel_range)
    {
    case MPU6050_ACCEL_4G:  lsb_per_g = 8192.0f;  break;
    case MPU6050_ACCEL_8G:  lsb_per_g = 4096.0f;  break;
    case MPU6050_ACCEL_16G: lsb_per_g = 2048.0f;  break;
    case MPU6050_ACCEL_2G:
    default:                lsb_per_g = 16384.0f; break;
    }

    return (float)raw / lsb_per_g;
}
