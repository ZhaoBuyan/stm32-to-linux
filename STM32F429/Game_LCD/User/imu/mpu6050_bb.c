/**
 ******************************************************************************
 * @file    mpu6050_bb.c
 * @brief   MPU6050 驱动 —— 自带一套软件模拟 I2C，跑在 PB6/PB7
 *
 * 为什么不复用野火的 i2c_Start()：
 *   野火那套软件 I2C 是给 GT911 触摸用的，它的引脚是【PH4/PH5】，
 *   不是 PB6/PB7（bsp_i2c_touch.h 里 I2C1/PB6/PB7 那段被 #if 0 关掉了）。
 *   复用会让 MPU6050 去读 PH4/PH5，永远不应答。
 *
 * 所以这里自己开一套：
 *   SCL = PB6，SDA = PB7（I2C1 的默认引脚，和任务书一致）
 *   两根都配成【开漏输出 + 上拉】：
 *     - 写 1 只是"松开"线上拉，写 0 才是拉低
 *     - 开漏模式下读 IDR 能拿到线的真实电平，所以不用切换输入/输出方向
 ******************************************************************************
 */
#include "mpu6050_bb.h"
#include "../systick/bsp_SysTick.h"     /* Delay_ms */

/* ---------------------------------------------------------------------------
 * 引脚定义（PB6 / PB7）
 * ------------------------------------------------------------------------- */
#define MPU_SCL_PORT        GPIOB
#define MPU_SCL_PIN         GPIO_Pin_6
#define MPU_SDA_PORT        GPIOB
#define MPU_SDA_PIN         GPIO_Pin_7

#define SCL_H()             GPIO_SetBits(MPU_SCL_PORT, MPU_SCL_PIN)
#define SCL_L()             GPIO_ResetBits(MPU_SCL_PORT, MPU_SCL_PIN)
#define SDA_H()             GPIO_SetBits(MPU_SDA_PORT, MPU_SDA_PIN)
#define SDA_L()             GPIO_ResetBits(MPU_SDA_PORT, MPU_SDA_PIN)
#define SDA_READ()          GPIO_ReadInputDataBit(MPU_SDA_PORT, MPU_SDA_PIN)

/* 寄存器 */
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_XOUT_H    0x3B
#define REG_PWR_MGMT_1      0x6B
#define REG_WHO_AM_I        0x75

#define MPU_WHOAMI_VALUE    0x68

/* ---------------------------------------------------------------------------
 * 软件 I2C 底层
 * ------------------------------------------------------------------------- */

/* 约 2~4us 的短延时（180MHz 下循环几十次），对应 ~100~200kHz 的 I2C */
static void i2c_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 30; i++) { __NOP(); }
}

static void i2c_init_pins(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = MPU_SCL_PIN | MPU_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_OD;        /* 开漏：能读线的真实电平 */
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;         /* 内部上拉（板上有外部上拉更好） */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    SCL_H();
    SDA_H();
}

static void i2c_start(void)
{
    SDA_H(); i2c_delay();
    SCL_H(); i2c_delay();
    SDA_L(); i2c_delay();          /* SCL 高时 SDA 下降 = 起始 */
    SCL_L(); i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L(); i2c_delay();
    SCL_H(); i2c_delay();
    SDA_H(); i2c_delay();          /* SCL 高时 SDA 上升 = 停止 */
}

/* 发一个字节，返回 0 = 收到 ACK，1 = NACK */
static uint8_t i2c_write_byte(uint8_t b)
{
    uint8_t i, nack;

    for (i = 0; i < 8; i++)
    {
        if (b & 0x80) { SDA_H(); } else { SDA_L(); }
        b <<= 1;
        i2c_delay();
        SCL_H(); i2c_delay();
        SCL_L(); i2c_delay();
    }

    SDA_H(); i2c_delay();          /* 释放 SDA，让从机应答 */
    SCL_H(); i2c_delay();
    nack = (SDA_READ() == Bit_SET) ? 1 : 0;
    SCL_L(); i2c_delay();

    return nack;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i, v = 0;

    SDA_H(); i2c_delay();          /* 释放 SDA 由从机驱动 */

    for (i = 0; i < 8; i++)
    {
        v <<= 1;
        SCL_H(); i2c_delay();
        if (SDA_READ() == Bit_SET) { v |= 1; }
        SCL_L(); i2c_delay();
    }

    /* 主机的应答位 */
    if (ack) { SDA_L(); } else { SDA_H(); }
    i2c_delay();
    SCL_H(); i2c_delay();
    SCL_L(); i2c_delay();
    SDA_H();

    return v;
}

/* ---------------------------------------------------------------------------
 * MPU6050 寄存器读写
 * ------------------------------------------------------------------------- */

static uint8_t mpu_wr(uint8_t reg, uint8_t val)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)MPU_ADDR8)) { i2c_stop(); return 1; }
    if (i2c_write_byte(reg))                { i2c_stop(); return 1; }
    if (i2c_write_byte(val))                { i2c_stop(); return 1; }
    i2c_stop();
    return 0;
}

static uint8_t mpu_rd(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    i2c_start();
    if (i2c_write_byte((uint8_t)MPU_ADDR8)) { i2c_stop(); return 1; }
    if (i2c_write_byte(reg))                { i2c_stop(); return 1; }

    i2c_start();                                     /* 重复起始 */
    if (i2c_write_byte((uint8_t)(MPU_ADDR8 | 0x01))) { i2c_stop(); return 1; }

    for (i = 0; i < len; i++)
    {
        buf[i] = i2c_read_byte((i == (uint16_t)(len - 1)) ? 0 : 1);
    }

    i2c_stop();
    return 0;
}

/* ---------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------- */

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

    dev->initialized  = 0;
    dev->gyro_bias[0] = 0;
    dev->gyro_bias[1] = 0;
    dev->gyro_bias[2] = 0;

    i2c_init_pins();
    Delay_ms(10);

    /* ① 验身份 */
    if (MPU_ReadWhoAmI(&id) != MPU_OK) { return MPU_ERR_WHOAMI; }

    /* ② 唤醒，时钟源用 X 轴陀螺 PLL */
    if (mpu_wr(REG_PWR_MGMT_1, 0x01)) { return MPU_ERR_I2C; }
    Delay_ms(50);

    /* ③ 采样率 1kHz/(1+4) = 200Hz */
    if (mpu_wr(REG_SMPLRT_DIV, 4)) { return MPU_ERR_I2C; }

    /* ④ 低通滤波 ~44Hz */
    if (mpu_wr(REG_CONFIG, 0x03)) { return MPU_ERR_I2C; }

    /* ⑤ 量程：陀螺 ±250°/s，加速度 ±2g */
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
