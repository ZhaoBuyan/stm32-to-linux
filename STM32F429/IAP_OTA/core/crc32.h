/**
 ******************************************************************************
 * @file    crc32.h
 * @brief   CRC-32（IEEE 802.3 / zlib 兼容），纯 C，可在 PC 上对拍
 *
 * 参数（任务书第 64 行要求「IEEE reflected/zlib 兼容参数」）：
 *   多项式（反射）  0xEDB88320
 *   初始值         0xFFFFFFFF
 *   输入/输出反射   是
 *   结果异或       0xFFFFFFFF
 *
 * 校验向量（写测试时用来对拍）：
 *   "123456789"  ->  0xCBF43926
 *   空输入        ->  0x00000000
 *
 * 为什么在这里自己实现而不用 HAL 的 CRC 外设：
 *   ① 外设的初值/反射方式要额外配置，容易和 PC 工具对不上
 *   ② 纯 C 版本能直接在 PC 上单测，保证和 `zlib.crc32` 逐位一致
 *   ③ Bootloader 里用一次、APP 里用一次，都要和 PC 的结果完全相同
 ******************************************************************************
 */
#ifndef __CRC32_H
#define __CRC32_H

#include <stdint.h>
#include <stddef.h>

/** 计算一段数据的 CRC32 */
uint32_t Crc32_Calc(const void *data, size_t len);

/** 增量计算：先 Crc32_Start()，中间多次 Crc32_Update()，最后 Crc32_Finish()
 *  镜像可能很大（几百 KB），分块喂进来就不用整块缓冲。 */
uint32_t Crc32_Start(void);
uint32_t Crc32_Update(uint32_t crc, const void *data, size_t len);
uint32_t Crc32_Finish(uint32_t crc);

#endif /* __CRC32_H */
