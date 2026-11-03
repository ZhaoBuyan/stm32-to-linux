/**
 ******************************************************************************
 * @file    mpu6050_bb.c
 * @brief   MPU6050 驱动实现（软件模拟 I2C）
 *
 * 关键点：
 *   1. 复用野火 bsp_i2c_touch.c 里的 i2c_Start/Stop/SendByte/ReadByte
 *   2. 上电后芯片处于 sleep，必须先清 PWR_MGMT_1 的 SLEEP 位
 *   3. 0x3B 起 14 字节连续，一次读完比读 7 次快
 *   4. 寄存器是【高字节在前】
 ******************************************************************************
 */
#include "mpu6050_bb.h"
#include "../systick/bsp_SysTick.h"     /* Delay_ms */

/* 野火 bsp_i2c_touch.c 提供的软件 I2C 原语 */
extern void    i2c_Start(void);
extern void    i2c_Stop(void);
extern void    i2c_SendByte(uint8_t _ucByte);
extern uint8_t i2c_ReadByte(void);
extern uint8_t i2c_WaitAck(void);
extern void    i2c_Ack(void);
extern void    i2c_NAck(void);

/* 寄存器 */
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_XOUT_H    0x3B
#define REG_PWR_MGMT_1      0x6B
#define REG_WHO_AM_I        0x75

#define MPU_WHOAMI_VALUE    0x68

/* ----------------------------------------------------------------------------
 * 软件 I2C 的读写封装
 * -------------------------------------------------------------------------- */

static uint8_t mpu_wr(uint8_t reg, uint8_t val)
{
    i2c_Start();
    i2c_SendByte(MPU_ADDR8);            /* 写 */
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    i2c_SendByte(reg);
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    i2c_SendByte(val);
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    i2c_Stop();
    return 0;
}

/* 从 reg 起连续读 len 字节 */
static uint8_t mpu_rd(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    i2c_Start();
    i2c_SendByte(MPU_ADDR8);            /* 写：先发寄存器地址 */
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    i2c_SendByte(reg);
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    i2c_Start();                        /* 重复起始 */
    i2c_SendByte((uint8_t)(MPU_ADDR8 | 0x01));   /* 读 */
    if (i2c_WaitAck()) { i2c_Stop(); return 1; }

    for (i = 0; i < len; i++)
    {
        buf[i] = i2c_ReadByte();
        if (i == (uint16_t)(len - 1)) { i2c_NAck(); }   /* 最后一个字节回 NACK */
        else                          { i2c_Ack();  }
    }

    i2c_Stop();
    return 0;
}

/* ----------------------------------------------------------------------------
 * 对外接口
 * -------------------------------------------------------------------------- */

MPU_Status MPU_ReadWhoAmI(uint8_t *id)
{
    uint8_t v = 0;

    if (id == 0) { return MPU_ERR_I2C; }
    if (mpu_rd(REG_WHO_AM_I, &v, 1)) { return MPU_ERR_I2C; }

    *id = v;
    return (v == MPU_WHOAMI_VALUE) ? MPU_OK : MPU_ERR_WHOAMI;
}

MPU_Status MPU_Init(MPU_Dev *dev)
{
    uint8_t id = 0;

    if (dev == 0) { return MPU_ERR_I2C; }

    dev->initialized    = 0;
    dev->gyro_bias[0]   = 0;
    dev->gyro_bias[1]   = 0;
    dev->gyro_bias[2]   = 0;

    /* ① 先验身份 */
    if (MPU_ReadWhoAmI(&id) != MPU_OK) { return MPU_ERR_WHOAMI; }

    /* ② 唤醒：清 SLEEP，时钟源用 X 轴陀螺 PLL */
    if (mpu_wr(REG_PWR_MGMT_1, 0x01)) { return MPU_ERR_I2C; }
    Delay_ms(50);

    /* ③ 采样率 1kHz/(1+4) = 200Hz */
    if (mpu_wr(REG_SMPLRT_DIV, 4)) { return MPU_ERR_I2C; }

    /* ④ 数字低通滤波 ~44Hz */
    if (mpu_wr(REG_CONFIG, 0x03)) { return MPU_ERR_I2C; }

    /* ⑤ 量程：陀螺 ±250°/s(0x00)，加速度 ±2g(0x00) */
    if (mpu_wr(REG_GYRO_CONFIG,  0x00)) { return MPU_ERR_I2C; }
    if (mpu_wr(REG_ACCEL_CONFIG, 0x00)) { return MPU_ERR_I2C; }

    dev->initialized = 1;
    return MPU_OK;
}

MPU_Status MPU_ReadRaw(MPU_Dev *dev, MPU_Raw *out)
{
    uint8_t b[14];

    if (dev == 0 || out == 0 || !dev->initialized) { return MPU_ERR_NOT_INIT; }
    if (mpu_rd(REG_ACCEL_XOUT_H, b, 14)) { return MPU_ERR_I2C; }

    out->ax   = (int16_t)(((uint16_t)b[0]  << 8) | b[1]);
    out->ay   = (int16_t)(((uint16_t)b[2]  << 8) | b[3]);
    out->az   = (int16_t)(((uint16_t)b[4]  << 8) | b[5]);
    out->temp = (int16_t)(((uint16_t)b[6]  << 8) | b[7]);
    out->gx   = (int16_t)(((uint16_t)b[8]  << 8) | b[9]);
    out->gy   = (int16_t)(((uint16_t)b[10] << 8) | b[11]);
    out->gz   = (int16_t)(((uint16_t)b[12] << 8) | b[13]);

    return MPU_OK;
}

MPU_Status MPU_CalibrateGyro(MPU_Dev *dev, uint16_t samples)
{
    int32_t sum[3] = { 0, 0, 0 };
    uint16_t i;
    MPU_Raw d;

    if (dev == 0 || !dev->initialized) { return MPU_ERR_NOT_INIT; }
    if (samples == 0) { samples = 1; }

    for (i = 0; i < samples; i++)
    {
        if (MPU_ReadRaw(dev, &d) != MPU_OK) { return MPU_ERR_I2C; }
        sum[0] += d.gx;
        sum[1] += d.gy;
        sum[2] += d.gz;
        Delay_ms(5);
    }

    dev->gyro_bias[0] = (int16_t)(sum[0] / (int32_t)samples);
    dev->gyro_bias[1] = (int16_t)(sum[1] / (int32_t)samples);
    dev->gyro_bias[2] = (int16_t)(sum[2] / (int32_t)samples);

    return MPU_OK;
}

void MPU_GetGyroCorrected(MPU_Dev *dev, const MPU_Raw *raw,
                          int16_t *gx, int16_t *gy, int16_t *gz)
{
    if (dev == 0 || raw == 0) { return; }
    if (gx) { *gx = (int16_t)(raw->gx - dev->gyro_bias[0]); }
    if (gy) { *gy = (int16_t)(raw->gy - dev->gyro_bias[1]); }
    if (gz) { *gz = (int16_t)(raw->gz - dev->gyro_bias[2]); }
}

float MPU_RawToTemp(int16_t raw_temp)
{
    return ((float)raw_temp / 340.0f) + 36.53f;
}
