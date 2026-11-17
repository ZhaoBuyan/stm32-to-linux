/**
 ******************************************************************************
 * @file    sdio_sd.h
 * @brief   SD 卡驱动（SDIO 接口，StdPeriph 库）
 *
 * 目标：把"卡插进去 → 能不能认出来 → 能不能读到里面的数据"这件事做通。
 * 文件系统（FatFs）在这之上，不在本模块的范围内。
 *
 * 几个刻意的设计选择：
 *
 * 1) **只用 1 位数据线**
 *    SDIO 支持 4 位，但要先 ACMD6 切宽度、还要发 8 个时钟让卡切换，
 *    时序坑不少。而写一个文本日志也就几十 KB/s，
 *    1 位 @17MHz 已经 2 MB/s，完全够用。先稳，再谈快。
 *
 * 2) **只用轮询，不用 DMA**
 *    第一次点亮硬件时，DMA 出错很难查（数据错了但没人报错）。
 *    轮询慢一点，但每一步都能看到状态标志，出问题好定位。
 *
 * 3) **容量从 CSD 算，不从"卡上印的字"猜**
 *    CSD 结构有两版：v1(SDSC) 和 v2(SDHC/SDXC)，容量公式完全不同。
 *    这里按 CSD 的版本字段分别算。
 *
 * 接线（底板 J28 TF 卡座）：
 *   CLK = PC12    CMD = PD2     D0 = PC8
 *   D1  = PC9     D2  = PC10    D3  = PC11
 *   本驱动只用 D0，D1~D3 不配置。
 ******************************************************************************
 */
#ifndef __SDIO_SD_H
#define __SDIO_SD_H

#include <stdint.h>

/* ---------------- 错误码 ---------------- */
typedef enum
{
    SD_OK = 0,
    SD_ERR_CMD_TIMEOUT,     /* 命令超时：卡没响应 */
    SD_ERR_CMD_CRC,         /* 命令 CRC 错 */
    SD_ERR_RESP,            /* 响应内容不对（例如 CMD8 回显不匹配）*/
    SD_ERR_NO_CARD,         /* CMD0/ACMD41 一直不就绪：没卡或卡坏 */
    SD_ERR_NOT_SD,          /* 是 MMC 卡，不是 SD 卡 */
    SD_ERR_DATA_TIMEOUT,    /* 数据传输超时 */
    SD_ERR_DATA_CRC,        /* 数据传输 CRC 错 */
    SD_ERR_PARAM            /* 参数不合法（块号越界等）*/
} SdError;

/* ---------------- 卡信息 ---------------- */
typedef struct
{
    /* 从 CID 解析 */
    uint8_t  mid;               /* 厂商 ID */
    uint16_t oid;               /* OEM/应用 ID */
    char     pnm[6];            /* 产品名，5 字符 + '\0' */
    uint8_t  prv;               /* 产品版本 */
    uint32_t psn;               /* 序列号 */

    uint16_t rca;               /* 相对卡地址 */

    /* 从 CSD 解析 */
    uint8_t  csd_version;       /* 0 = v1(SDSC), 1 = v2(SDHC/SDXC) */
    uint8_t  is_sdhc;           /* 1 = 按块寻址（SDHC/SDXC）*/
    uint32_t block_count;       /* 512 字节块总数 */
    uint32_t capacity_kb;       /* 容量（KB）*/
    uint32_t capacity_mb;       /* 容量（MB），方便打印 */
} SdCardInfo;

/* ---------------- 接口 ---------------- */

/** 初始化 SDIO 并识别卡
 *  成功时把 {@param info} 填满（可以是 NULL，只要检测结果）
 *  @return SD_OK 表示卡已就绪，可以读写
 */
SdError Sd_Init(SdCardInfo *info);

/** 错误码的可读名，用于打印 */
const char *Sd_ErrorName(SdError e);

/** 读若干个 512 字节块（轮询方式）
 *  @param block  起始块号（SDHC 用块号；SDSC 内部会换算成字节地址）
 *  @param buf    目标缓冲，必须 >= count * 512 字节
 *  @param count  块数，>= 1
 */
SdError Sd_ReadBlocks(uint32_t block, uint8_t *buf, uint32_t count);

/** 当前是否已经初始化成功 */
int Sd_IsReady(void);

#endif /* __SDIO_SD_H */
