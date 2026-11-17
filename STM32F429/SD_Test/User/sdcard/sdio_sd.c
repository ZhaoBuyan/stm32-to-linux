/**
 ******************************************************************************
 * @file    sdio_sd.c
 * @brief   SD 卡驱动实现（SDIO + StdPeriph，1 位模式，轮询）
 *
 * 识别流程（SD 规范规定的顺序，不能跳）：
 *   CMD0   GO_IDLE_STATE      复位到 idle
 *   CMD8   SEND_IF_COND       探测是不是 v2 卡（SDHC/SDXC 的可能性）
 *   CMD55  APP_CMD            下面要发的是 ACMD
 *   ACMD41 SD_SEND_OP_COND    轮询直到卡上电完成（这是最耗时的循环）
 *   CMD2   ALL_SEND_CID       读 CID（136 位长响应）
 *   CMD3   SEND_RELATIVE_ADDR 让卡报一个 RCA
 *   CMD9   SEND_CSD           读 CSD（容量信息在里面）
 *   CMD7   SELECT_CARD        选中这张卡（之后才能读写数据）
 *
 * 读数据：
 *   CMD17  单块读 / CMD18 多块读 + CMD12 停止
 ******************************************************************************
 */
#include "sdio_sd.h"

#include <string.h>

#include "stm32f4xx.h"

/* ---------------- 超时（循环次数，不是毫秒）----------------
 * 用循环次数而不是 Tick_Get()，是为了让驱动不依赖任何时基模块，
 * 移植到别的工程时只要改 SDIO 相关部分。
 * 这些值在 180MHz 下留了很大余量，正常卡远用不到。 */
#define SD_CMD_TIMEOUT      0x00030000u
#define SD_DATA_TIMEOUT     0x00A00000u
#define SD_INIT_RETRY       200u        /* ACMD41 最多轮询次数 */

/* ---------------- 时钟 ----------------
 * SDIOCLK = PLL_VCO / PLL_Q = (25MHz/25*360) / 7 ≈ 51.43 MHz
 *   识别阶段：51.43MHz / (127+2) ≈ 399 kHz   （规范要求 <= 400kHz）
 *   数据阶段：51.43MHz / (1+2)   ≈ 17.1 MHz  （默认速度卡上限 25MHz）
 * 注意 CLKDIV 是"分频系数 - 2"，所以写 127 表示除以 129。 */
#define SD_INIT_CLKDIV      127u
#define SD_FAST_CLKDIV      1u

static uint8_t s_ready  = 0;
static uint8_t s_isdhc  = 0;    /* 1 = 按块寻址（SDHC/SDXC）*/

/* ============================================================================
 * 底层工具
 * ========================================================================== */

/** 等某个标志置位，超时返回 0 */
static int wait_flag(uint32_t flag, uint32_t loops)
{
    while (loops-- > 0)
    {
        if (SDIO_GetFlagStatus(flag) != RESET) { return 1; }
    }
    return 0;
}

/** 发一条命令，不处理数据
 *  @param resp_mode   SDIO_Response_No / Short / Long
 */
static SdError send_cmd(uint8_t idx, uint32_t arg, uint32_t resp_mode,
                        uint8_t wait_int)
{
    SDIO_CmdInitTypeDef cmd;

    /* 发之前把上一次的标志清干净，否则会读到假结果 */
    SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CMDREND |
                   SDIO_FLAG_CTIMEOUT | SDIO_FLAG_CMDACT);

    cmd.SDIO_Argument   = arg;
    cmd.SDIO_CmdIndex   = idx;
    cmd.SDIO_Response   = resp_mode;
    cmd.SDIO_Wait       = wait_int;
    cmd.SDIO_CPSM       = SDIO_CPSM_Enable;
    SDIO_SendCommand(&cmd);

    /* 无响应命令：等 CMDSENT */
    if (resp_mode == SDIO_Response_No)
    {
        if (!wait_flag(SDIO_FLAG_CMDSENT, SD_CMD_TIMEOUT)) { return SD_ERR_CMD_TIMEOUT; }
        SDIO_ClearFlag(SDIO_FLAG_CMDSENT);
        return SD_OK;
    }

    /* 有响应：等 CMDREND（或 CTIMEOUT / CCRCFAIL）*/
    {
        uint32_t loops = SD_CMD_TIMEOUT;

        while (loops-- > 0)
        {
            if (SDIO_GetFlagStatus(SDIO_FLAG_CMDREND) != RESET)
            {
                SDIO_ClearFlag(SDIO_FLAG_CMDREND);
                return SD_OK;
            }
            if (SDIO_GetFlagStatus(SDIO_FLAG_CTIMEOUT) != RESET)
            {
                SDIO_ClearFlag(SDIO_FLAG_CTIMEOUT);
                return SD_ERR_CMD_TIMEOUT;
            }
            if (SDIO_GetFlagStatus(SDIO_FLAG_CCRCFAIL) != RESET)
            {
                SDIO_ClearFlag(SDIO_FLAG_CCRCFAIL);

                /* ⚠️ 关键：idle 状态下的这些命令，卡回的响应**没有有效 CRC**，
                 *    SDIO 硬件必然置 CCRCFAIL。如果当成错误处理，
                 *    ACMD41 会永远"失败"，卡怎么也认不出来。
                 *    规范里 CMD0/CMD1/CMD8/CMD55/ACMD41 的响应本来就不做 CRC 校验。
                 *    这些命令的响应内容仍然可用，所以直接当成功。 */
                if (idx == 0u || idx == 1u || idx == 8u ||
                    idx == 55u || idx == 41u)
                {
                    return SD_OK;
                }
                return SD_ERR_CMD_CRC;
            }
        }
    }
    return SD_ERR_CMD_TIMEOUT;
}

/** 把 136 位长响应拼成 16 字节（高位在前，和 CSD/CID 的字节序一致）*/
static void get_long_response(uint8_t out[16])
{
    uint32_t r1 = SDIO_GetResponse(SDIO_RESP1);
    uint32_t r2 = SDIO_GetResponse(SDIO_RESP2);
    uint32_t r3 = SDIO_GetResponse(SDIO_RESP3);
    uint32_t r4 = SDIO_GetResponse(SDIO_RESP4);

    out[0]  = (uint8_t)(r1 >> 24); out[1]  = (uint8_t)(r1 >> 16);
    out[2]  = (uint8_t)(r1 >> 8);  out[3]  = (uint8_t)(r1);
    out[4]  = (uint8_t)(r2 >> 24); out[5]  = (uint8_t)(r2 >> 16);
    out[6]  = (uint8_t)(r2 >> 8);  out[7]  = (uint8_t)(r2);
    out[8]  = (uint8_t)(r3 >> 24); out[9]  = (uint8_t)(r3 >> 16);
    out[10] = (uint8_t)(r3 >> 8);  out[11] = (uint8_t)(r3);
    out[12] = (uint8_t)(r4 >> 24); out[13] = (uint8_t)(r4 >> 16);
    out[14] = (uint8_t)(r4 >> 8);  out[15] = (uint8_t)(r4);
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

static void sdio_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOD, ENABLE);

    /* 只用 D0（PC8），CMD（PD2），CLK（PC12）。
     * D1~D3（PC9/PC10/PC11）不配置，因为我们不开 4 位模式。 */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource8,  GPIO_AF_SDIO);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource12, GPIO_AF_SDIO);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource2,  GPIO_AF_SDIO);

    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;     /* CMD/D0 上拉，避免空卡时悬空 */

    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_12;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOD, &gpio);
}

static void sdio_hw_init(uint32_t clkdiv)
{
    SDIO_InitTypeDef init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SDIO, ENABLE);

    /* 完全重新初始化：断电再上电，避免上一次失败留下的状态 */
    SDIO_DeInit();
    SDIO_SetPowerState(SDIO_PowerState_OFF);

    init.SDIO_ClockDiv        = (uint8_t)clkdiv;
    init.SDIO_ClockEdge       = SDIO_ClockEdge_Rising;
    init.SDIO_ClockBypass     = SDIO_ClockBypass_Disable;   /* 用分频，不旁路 */
    init.SDIO_ClockPowerSave  = SDIO_ClockPowerSave_Disable;
    init.SDIO_BusWide         = SDIO_BusWide_1b;
    init.SDIO_HardwareFlowControl = SDIO_HardwareFlowControl_Disable;

    SDIO_Init(&init);
    SDIO_SetPowerState(SDIO_PowerState_ON);
}

/* 只改 SDIO 分频，其余配置和电源状态保持不动。
 * SDIO_Init 只重写 CLKCR 的配置位（不含 CLKEN），所以不会产生时钟中断。 */
static void sdio_set_clock(uint32_t clkdiv)
{
    SDIO_InitTypeDef init;

    init.SDIO_ClockDiv        = (uint8_t)clkdiv;
    init.SDIO_ClockEdge       = SDIO_ClockEdge_Rising;
    init.SDIO_ClockBypass     = SDIO_ClockBypass_Disable;
    init.SDIO_ClockPowerSave  = SDIO_ClockPowerSave_Disable;
    init.SDIO_BusWide         = SDIO_BusWide_1b;
    init.SDIO_HardwareFlowControl = SDIO_HardwareFlowControl_Disable;

    SDIO_Init(&init);
}
/* CMD0 + CMD8：判断是 v1 还是 v2 卡，并让卡进入 idle */
static SdError card_probe(uint32_t *is_v2)
{
    uint32_t resp;

    *is_v2 = 0;

    /* CMD0：无响应。真卡一定成功；没卡时也会"成功"（因为不需要响应），
     * 所以后面必须靠 ACMD41 才能真正区分有没有卡。 */
    (void)send_cmd(0, 0x00000000u, SDIO_Response_No, SDIO_Wait_No);

    /* 复位后按规范要至少 74 个时钟，SDIO 外设会自动处理 */

    /* CMD8：arg = 0x1AA（VHS=1 表示 2.7~3.6V，检查图样 0xAA）
     * v1 卡不认这条命令，会超时 -> 说明是 v1 */
    if (send_cmd(8, 0x000001AAu, SDIO_Response_Short, SDIO_Wait_No) == SD_OK)
    {
        resp = SDIO_GetResponse(SDIO_RESP1);

        /* 高 12 位必须是 0x1AA，否则响应不可信 */
        if ((resp & 0x00000FFFu) != 0x1AAu) { return SD_ERR_RESP; }

        *is_v2 = 1;
    }

    return SD_OK;
}

/* ACMD41 轮询：等卡上电完成。总耗时可能几百毫秒 */
static SdError card_power_on(uint32_t is_v2)
{
    uint32_t i;
    uint32_t arg;

    /* HCS 位：告诉 v2 卡"主机支持 SDHC/SDXC"。
     * 对 v1 卡不能置这个位，否则会被当成非法参数。 */
    arg = is_v2 ? 0x40FF8000u : 0x00FF8000u;

    for (i = 0; i < SD_INIT_RETRY; i++)
    {
        uint32_t resp;

        /* 每条 ACMD 前面必须先发 CMD55（APP_CMD） */
        if (send_cmd(55, 0x00000000u, SDIO_Response_Short, SDIO_Wait_No) != SD_OK)
        {
            continue;       /* 卡可能还没准备好，继续轮询 */
        }

        if (send_cmd(41, arg, SDIO_Response_Short, SDIO_Wait_No) != SD_OK)
        {
            continue;
        }

        resp = SDIO_GetResponse(SDIO_RESP1);

        /* bit31 = 1 表示上电完成 */
        if ((resp & 0x80000000u) != 0u)
        {
            return SD_OK;
        }
    }

    return SD_ERR_NO_CARD;
}

/* 从 CID 解析厂商、产品名、序列号 */
static void parse_cid(const uint8_t cid[16], SdCardInfo *info)
{
    unsigned i;

    info->mid = cid[0];
    info->oid = (uint16_t)((cid[1] << 8) | cid[2]);

    for (i = 0; i < 5; i++) { info->pnm[i] = (char)cid[3 + i]; }
    info->pnm[5] = '\0';

    info->prv = cid[8];
    info->psn = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16) |
                ((uint32_t)cid[11] << 8) | (uint32_t)cid[12];
}

/* 从 CSD 算容量。v1 和 v2 的公式完全不同，必须分开算 */
static void parse_csd(const uint8_t csd[16], SdCardInfo *info)
{
    info->csd_version = (uint8_t)((csd[0] >> 6) & 0x03u);

    if (info->csd_version == 1)
    {
        /* CSD v2.0：SDHC / SDXC，按块寻址，容量单位直接是 512KB */
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                          ((uint32_t)csd[8] << 8) |
                          (uint32_t)csd[9];

        info->is_sdhc      = 1;
        info->capacity_kb  = (c_size + 1u) * 512u;      /* 1 单位 = 512KB */
        info->block_count  = (c_size + 1u) * 1024u;     /* 1 单位 = 1024 块 */
    }
    else
    {
        /* CSD v1.0：SDSC，按字节寻址
         * capacity = (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN */
        uint32_t read_bl_len = (uint32_t)(csd[5] & 0x0Fu);
        uint32_t c_size      = ((uint32_t)(csd[6] & 0x03u) << 10) |
                               ((uint32_t)csd[7] << 2) |
                               ((uint32_t)(csd[8] & 0xC0u) >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03u) << 1) |
                               ((uint32_t)(csd[10] & 0x80u) >> 7);
        uint32_t block_len   = 1u << read_bl_len;
        uint32_t blocknr     = (c_size + 1u) << (c_size_mult + 2u);

        info->is_sdhc     = 0;
        /* 先算字节数再转 KB。SDSC 最大 2GB，32 位够用 */
        info->capacity_kb = (blocknr * block_len) / 1024u;
        info->block_count = (blocknr * block_len) / 512u;
    }

    info->capacity_mb = info->capacity_kb / 1024u;
}

SdError Sd_Init(SdCardInfo *info)
{
    SdCardInfo  local;
    uint8_t     cid[16];
    uint8_t     csd[16];
    uint32_t    is_v2 = 0;
    uint32_t    rca  = 0;
    SdError     err;
    uint32_t    resp;

    s_ready = 0;
    memset(&local, 0, sizeof(local));

    sdio_gpio_init();

    /* ---------- 1. 先用 400kHz 识别 ---------- */
    sdio_hw_init(SD_INIT_CLKDIV);

    err = card_probe(&is_v2);
    if (err != SD_OK) { return err; }

    err = card_power_on(is_v2);
    if (err != SD_OK) { return err; }

    /* ---------- 2. 读 CID ---------- */
    err = send_cmd(2, 0x00000000u, SDIO_Response_Long, SDIO_Wait_No);
    if (err != SD_OK) { return err; }
    get_long_response(cid);
    parse_cid(cid, &local);

    /* ---------- 3. 要 RCA ---------- */
    err = send_cmd(3, 0x00000000u, SDIO_Response_Short, SDIO_Wait_No);
    if (err != SD_OK) { return err; }
    resp = SDIO_GetResponse(SDIO_RESP1);
    rca  = (resp >> 16) & 0xFFFFu;

    /* RCA 为 0 说明卡还没就绪，按规范这算失败 */
    if (rca == 0u) { return SD_ERR_NO_CARD; }
    local.rca = (uint16_t)rca;

    /* ---------- 4. 读 CSD ---------- */
    err = send_cmd(9, rca << 16, SDIO_Response_Long, SDIO_Wait_No);
    if (err != SD_OK) { return err; }
    get_long_response(csd);
    parse_csd(csd, &local);

    /* ---------- 5. 选中卡 ---------- */
    err = send_cmd(7, rca << 16, SDIO_Response_Short, SDIO_Wait_No);
    if (err != SD_OK) { return err; }

    /* ---------- 6. SDSC 要显式设块长；SDHC 固定 512 ---------- */
    if (!local.is_sdhc)
    {
        err = send_cmd(16, 512u, SDIO_Response_Short, SDIO_Wait_No);
        if (err != SD_OK) { return err; }
    }

    /* ---------- 7. 切到高速时钟 ----------
     * ⚠️ 只能改分频，**不能**再调 sdio_hw_init()：
     *    那里面有 SDIO_DeInit + PowerState_OFF，
     *    会给卡断电 -> 卡退回 idle -> 刚拿到的 RCA 和选中状态全丢，
     *    后面读数据必然失败。
     *    改时钟只需重写 CLKCR 的分频位（CLKEN 位保持不变，不会产生时钟毛刺）。
     *
     * 另外这里**不切 4 位模式**：写文本日志 1 位就够，
     * 而切 4 位要 ACMD6 + 8 个时钟，坑多，先不碰。 */
    sdio_set_clock(SD_FAST_CLKDIV);

    s_isdhc = local.is_sdhc;
    s_ready = 1;

    if (info != 0) { *info = local; }
    return SD_OK;
}

const char *Sd_ErrorName(SdError e)
{
    switch (e)
    {
    case SD_OK:                return "OK";
    case SD_ERR_CMD_TIMEOUT:   return "cmd timeout (no card?)";
    case SD_ERR_CMD_CRC:       return "cmd crc error";
    case SD_ERR_RESP:          return "bad response";
    case SD_ERR_NO_CARD:       return "no card / not ready";
    case SD_ERR_NOT_SD:        return "not an SD card";
    case SD_ERR_DATA_TIMEOUT:  return "data timeout";
    case SD_ERR_DATA_CRC:      return "data crc error";
    case SD_ERR_PARAM:         return "bad parameter";
    default:                   return "?";
    }
}

int Sd_IsReady(void) { return s_ready; }

/* ============================================================================
 * 读数据（轮询）
 * ========================================================================== */

/* 从 SDIO FIFO 里读 len 字节到 buf */
static SdError read_fifo(uint8_t *buf, uint32_t len)
{
    uint32_t words = len / 4u;
    uint32_t i = 0;
    uint32_t guard = SD_DATA_TIMEOUT;
    uint32_t tmp;

    while (i < words)
    {
        if (SDIO_GetFlagStatus(SDIO_FLAG_DCRCFAIL) != RESET)
        {
            SDIO_ClearFlag(SDIO_FLAG_DCRCFAIL);
            return SD_ERR_DATA_CRC;
        }
        if (SDIO_GetFlagStatus(SDIO_FLAG_DTIMEOUT) != RESET)
        {
            SDIO_ClearFlag(SDIO_FLAG_DTIMEOUT);
            return SD_ERR_DATA_TIMEOUT;
        }

        if (SDIO_GetFlagStatus(SDIO_FLAG_RXDAVL) != RESET)
        {
            tmp = SDIO_ReadData();
            /* 用 memcpy 而不是直接写 uint32_t*，避免 buf 未对齐时
             * 在 Cortex-M4 上触发 HardFault（非对齐访问）*/
            memcpy(&buf[i * 4u], &tmp, 4);
            i++;
        }
        else if (--guard == 0u)
        {
            return SD_ERR_DATA_TIMEOUT;
        }
    }

    /* 等这一批数据真正结束 */
    if (!wait_flag(SDIO_FLAG_DBCKEND, SD_DATA_TIMEOUT))
    {
        return SD_ERR_DATA_TIMEOUT;
    }
    SDIO_ClearFlag(SDIO_FLAG_DBCKEND);

    return SD_OK;
}

SdError Sd_ReadBlocks(uint32_t block, uint8_t *buf, uint32_t count)
{
    SDIO_DataInitTypeDef data;
    SdError err;
    uint32_t len;

    if (!s_ready)          { return SD_ERR_NO_CARD; }
    if (buf == 0 || count == 0u) { return SD_ERR_PARAM; }

    len = count * 512u;

    /* 地址怎么算，取决于卡的寻址方式（这是 SD 规范里最容易出错的一点）：
     *   SDHC/SDXC：命令参数就是**块号**
     *   SDSC     ：命令参数是**字节地址**，所以要乘 512
     * 调用方统一按"块号"传，换算在这里做。 */
    {
        uint32_t addr = s_isdhc ? block : (block * 512u);

        /* 单块用 CMD17，多块用 CMD18 */
        err = send_cmd((count == 1u) ? 17u : 18u, addr,
                       SDIO_Response_Short, SDIO_Wait_No);
        if (err != SD_OK) { return err; }
    }

    /* 配数据通道 */
    data.SDIO_DataTimeOut  = SD_DATA_TIMEOUT;
    data.SDIO_DataLength   = len;
    data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;   /* 块长固定 512 */
    data.SDIO_TransferDir  = SDIO_TransferDir_ToSDIO;
    data.SDIO_TransferMode = SDIO_TransferMode_Block;
    data.SDIO_DPSM         = SDIO_DPSM_Enable;
    SDIO_DataConfig(&data);

    err = read_fifo(buf, len);
    if (err != SD_OK) { return err; }

    /* 多块读要显式停止 */
    if (count > 1u)
    {
        (void)send_cmd(12, 0x00000000u, SDIO_Response_Short, SDIO_Wait_No);
    }

    return SD_OK;
}
