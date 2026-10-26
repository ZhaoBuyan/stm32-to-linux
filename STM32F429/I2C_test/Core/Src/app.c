/**
 ******************************************************************************
 * @file    app.c
 * @brief   业务层实现：参数加载 / 保存 / 显示 / 按键
 *
 * 职责：结构体打包解包、magic 与校验判定、出厂默认、上电加载、屏幕分区显示、按键分发
 ******************************************************************************
 */
#include "app.h"
#include "eeprom.h"
#include "oled.h"
#include <stdio.h>

static sys_param_t s_param;          /* 当前生效的参数（RAM 里的一份） */
static uint8_t     s_eeOk = 0;       /* 最近一次 EEPROM 操作是否成功 */
static uint8_t     s_needRefresh = 1;

/* ======================== 出厂默认值 ======================== */
static void APP_LoadFactory(void)
{
    s_param.devId    = APP_DEFAULT_DEV_ID;
    s_param.runCount = 0;
    s_param.fwVer    = APP_DEFAULT_FW_VER;
}

/* ==================== 参数打包 / 解包 ====================
 * EEPROM 只能按字节存，所以要把结构体"摊平"成一串字节。
 * 不用 memcpy(结构体)，因为那样会带上编译器填充字节，
 * 换成别的编译器/优化等级就变了 —— 存到 EEPROM 里必须逐字段拆。
 * ======================================================== */
static void APP_Pack(uint8_t *buf)
{
    buf[0] = APP_MAGIC;
    buf[1] = (uint8_t)(s_param.devId >> 8);        /* 高字节在前 */
    buf[2] = (uint8_t)(s_param.devId & 0xFF);
    buf[3] = (uint8_t)(s_param.runCount >> 24);    /* 32 位计数，大端 */
    buf[4] = (uint8_t)(s_param.runCount >> 16);
    buf[5] = (uint8_t)(s_param.runCount >> 8);
    buf[6] = (uint8_t)(s_param.runCount & 0xFF);
    buf[7] = (uint8_t)(s_param.fwVer >> 8);
    buf[8] = (uint8_t)(s_param.fwVer & 0xFF);
    buf[9] = 0;                                    /* 校验字节，见 APP_Checksum */
}

static void APP_Unpack(const uint8_t *buf)
{
    s_param.devId    = (uint16_t)((buf[1] << 8) | buf[2]);
    s_param.runCount = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[4] << 16)
                     | ((uint32_t)buf[5] << 8)  |  (uint32_t)buf[6];
    s_param.fwVer    = (uint16_t)((buf[7] << 8) | buf[8]);
}

/**
 * @brief  校验字节（拓展任务）
 *
 * 算法：逐字节累加，取低 8 位。读回来时用同样算法重算，和 buf[9] 比对，
 *       不一致就认为数据损坏（例如写一半掉电）。
 *
 * 局限（答辩可以主动说）：累加和查不出"字节顺序被打乱"和"两个字节互换"。
 *       要更强用 CRC-8（poly 0x07）或 异或+累加 的组合。
 */
static uint8_t APP_Checksum(const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    uint8_t sum = 0;

    for (i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + buf[i]);
    }

    return sum;
}

/* ======================== 存 / 取 ======================== */
static void APP_Save(void)
{
    uint8_t buf[EE_PARAM_BYTES];

    APP_Pack(buf);
    buf[9] = APP_Checksum(buf, 9);     /* 校验只覆盖前 9 字节 */

    /* 整块写入：EE_WriteBuf 内部按 8 字节分页并等写周期 */
    s_eeOk = (EE_WriteBuf(EE_ADDR_MAGIC, buf, EE_PARAM_BYTES) == HAL_OK) ? 1 : 0;
}

static void APP_Load(void)
{
    uint8_t buf[EE_PARAM_BYTES];

    if (EE_ReadBuf(EE_ADDR_MAGIC, buf, EE_PARAM_BYTES) != HAL_OK)
    {
        APP_LoadFactory();
        s_eeOk = 0;
        return;
    }

    /* 判"有没有存过"：magic 不对就是第一次上电 / 芯片是新的 */
    if (buf[0] != APP_MAGIC)
    {
        APP_LoadFactory();
        APP_Save();                    /* 顺便把出厂值写进去 */
        return;
    }

    /* 判"存的对不对"：校验（拓展任务实现了才会起作用） */
    if (APP_Checksum(buf, 9) != buf[9])
    {
        APP_LoadFactory();
        APP_Save();
        return;
    }

    APP_Unpack(buf);
    s_eeOk = 1;
}

/* ======================== 对外接口 ======================== */
void APP_Init(void)
{
    APP_Load();
    s_needRefresh = 1;
}

void APP_KeyUp(void)
{
    s_param.runCount++;
    APP_Save();
    s_needRefresh = 1;
}

void APP_KeyDown(void)
{
    APP_LoadFactory();
    APP_Save();
    s_needRefresh = 1;
}

const sys_param_t *APP_GetParam(void)
{
    return &s_param;
}

/**
 * @brief  重画整屏（分区域显示三组参数）
 * @note   64 行 / 每行 16 像素 = 4 行，所以用 8x16 字号。
 *         如果老师要求"翻页显示"，加一个 page 变量按时间或按键切换即可。
 */
void APP_Display(void)
{
    char line[24];

    OLED_CLS();

    /* 第 1 块：设备 ID */
    OLED_ShowStr(0, 0, "DEV.ID:", OLED_SIZE_8X16);
    OLED_ShowNum(56, 0, s_param.devId, OLED_SIZE_8X16);

    /* 第 2 块：运行计数 */
    OLED_ShowStr(0, 2, "RUN  :", OLED_SIZE_8X16);
    OLED_ShowNum(56, 2, s_param.runCount, OLED_SIZE_8X16);

    /* 第 3 块：固件版本（显示成 v1.00 的样子） */
    sprintf(line, "FW   :v%u.%02u",
            (unsigned)(s_param.fwVer >> 8),
            (unsigned)(s_param.fwVer & 0xFF));
    OLED_ShowStr(0, 4, line, OLED_SIZE_8X16);

    /* 第 4 块：EEPROM 状态 —— 调试期非常有用 */
    OLED_ShowStr(0, 6, s_eeOk ? "EEPROM:OK" : "EEPROM:ERR", OLED_SIZE_6X8);

    s_needRefresh = 0;
}

/* 主循环每轮调用：需要刷新时才重画，避免每轮都刷屏（I2C 很慢） */
void APP_Task(void)
{
    if (s_needRefresh)
    {
        APP_Display();
    }
}
