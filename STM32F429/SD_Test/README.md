# SD_Test —— SD 卡检测（SDIO，1 位模式）

> 目标：**插卡 → 认出来 → 读出第 0 块的真实内容**。
> 做完这一步，"检测存储器"就通了。文件系统（FatFs）是下一步，不在本工程范围。

---

## 一、硬件

**底板上自带 TF 卡座 `J28`，不用外接模块。** 插一张 microSD 卡即可。

原理图第 8 页 `Camera/SD V2.0` 的接线：

| 卡座 J28 | MCU 引脚 | 说明 |
|---|---|---|
| CLK (5) | **PC12** | SDIO_CLK |
| CMD (3) | **PD2** | SDIO_CMD |
| DAT0 (7) | **PC8** | SDIO_D0 —— 本驱动只用这一根 |
| DAT1 (8) | PC9 | 未配置（不开 4 位模式）|
| DAT2 (1) | PC10 | 未配置 |
| CD/DAT3 (2) | PC11 | 未配置 |
| VDD (4) | 3V3 | 卡座供电 |
| VSS (6) | GND | |

> ⚠️ 这页叫 `Camera/SD` —— SDIO 和摄像头（DCMI）**复用同一组引脚**，二选一。

**这些引脚和 LTDC / FMC / GPS 串口（PB10/PB11）/ EC11 规划（PB12~PB14）都不冲突。**

---

## 二、时钟

```
PLL: HSE 25MHz, M=25, N=360, P=2, Q=7
  VCO      = 25/25 * 360 = 360 MHz
  SYSCLK   = 360 / 2     = 180 MHz
  SDIOCLK  = 360 / 7     = 51.43 MHz     ← Q=7 决定
```

SDIO 时钟由 `CLKDIV` 分频，**实际频率 = SDIOCLK / (CLKDIV + 2)**：

| 阶段 | CLKDIV | 实际频率 | 为什么 |
|---|---|---|---|
| 识别 | 127 | 51.43/129 ≈ **399 kHz** | 规范要求识别阶段 ≤ 400 kHz |
| 数据 | 1 | 51.43/3 ≈ **17.1 MHz** | 默认速度卡上限 25 MHz，留余量 |

> 注意 `CLKDIV` 是"分频系数 **减 2**"，写 127 表示除以 129 —— 这是 SDIO 外设的特性，很容易算错。

---

## 三、识别流程

按 SD 规范顺序，不能跳：

```
CMD0    GO_IDLE_STATE        复位到 idle（无响应）
CMD8    SEND_IF_COND         探测是否 v2 卡（SDHC/SDXC），回显 0x1AA
CMD55   APP_CMD              ↓ 循环
ACMD41  SD_SEND_OP_COND      等卡上电完成（bit31=1），v2 卡带 HCS 位
CMD2    ALL_SEND_CID         读 CID（136 位长响应）
CMD3    SEND_RELATIVE_ADDR   要一个 RCA
CMD9    SEND_CSD             读 CSD（容量在里面）
CMD7    SELECT_CARD          选中卡（之后才能读写）
CMD16   SET_BLOCKLEN 512     只有 SDSC 需要，SDHC 固定 512
```

### 三个容易踩的坑（都在这版代码里处理了）

**① `ACMD41` / `CMD55` 的响应没有有效 CRC**

idle 状态下卡回的响应不算 CRC，SDIO 硬件必然置 `CCRCFAIL`。
如果当成错误处理，**ACMD41 会永远"失败"，卡怎么也认不出来**。
规范里 `CMD0/CMD1/CMD8/CMD55/ACMD41` 本来就不做 CRC 校验，代码里单独放行。

**② 提速不能重新初始化 SDIO**

`SDIO_DeInit()` + `SetPowerState(OFF)` 会给卡**断电** → 卡退回 idle →
刚拿到的 RCA 和选中状态全丢 → 后面读数据必然失败。
所以提速只重写 `CLKCR` 的分频位（`CLKEN` 不变，无时钟毛刺），见 `sdio_set_clock()`。

**③ 地址怎么算取决于卡的寻址方式**

```
SDHC/SDXC（CSD v2）：命令参数 = 块号
SDSC     （CSD v1）：命令参数 = 字节地址（块号 × 512）
```

---

## 四、容量的算法

CSD 有两个版本，**公式完全不同**，必须按版本分开算：

```
CSD v2.0（SDHC/SDXC）
    capacity = (C_SIZE + 1) × 512 KB
    block_count = (C_SIZE + 1) × 1024

CSD v1.0（SDSC）
    capacity = (C_SIZE+1) × 2^(C_SIZE_MULT+2) × 2^READ_BL_LEN  字节
```

代码在 `parse_csd()`。

---

## 五、烧进去会看到什么

### 成功

```
============== SD CARD TEST ==============
[1] init      SDIO ok, card identified   PASS
[2] type      SDHC   (CSD v2, block addressing)
[3] capacity  30436 MB  (29.7 GB)
[4] blocks    62333952 x 512B
[5] cid       MID 0x03  OID 0x5344  PNM "SD32G"  PRV 8.0
[6] serial    0x12345678   RCA 0x0001
[7] read blk0 OK  (MBR partition table at 510)   PASS
              first 16 bytes: 33 C0 FA 8E D0 BC 00 7C 8E C0 8E D8 8E D0 BC
              bytes 510/511: 55 AA
[8] verdict   CARD USABLE, ready for FatFs
==========================================
```

**`[7] read blk0` 是关键** —— 前面几条都可能"看起来成功"，
只有真的读出数据，才证明命令、时钟、数据通道全对。

### 没插卡 / 认不到

```
[1] init      FAIL: no card / not ready
              check: card inserted? TF socket J28
                     contacts clean? FAT32 formatted?
```

屏幕显示 `NO CARD`，并且**主循环每 5 秒重试一次** —— 你后插卡也能自动认到。

### 卡能读但是没格式化

```
[7] read blk0 OK  (no known signature)   PASS
[8] verdict   card readable but NOT FORMATTED (no MBR/FAT)
              format it as FAT32 on a PC first
```

---

## 六、第 0 块的签名怎么判断

| 条件 | 含义 |
|---|---|
| 偏移 510/511 = `55 AA` | **MBR 分区表**（绝大多数卡）|
| 首字节 `EB`/`E9`，且偏移 54 或 82 处有 `"FAT"` | **无分区表的 FAT**（superfloppy）|
| 都不满足 | 未格式化，或不是 FAT |

**这一步能判断"卡里的数据能不能被 PC 和 FatFs 认出来"**，为加 FatFs 做准备。

---

## 七、这一版**没做**的事（以及为什么）

| 没做 | 原因 |
|---|---|
| **4 位数据线** | 要先 ACMD6 切宽度、再发 8 个时钟让卡切换，时序坑多。写文本日志几十 KB/s，1 位 @17MHz 有 2 MB/s，够用。**先稳，再谈快** |
| **DMA** | 第一次点亮硬件时 DMA 出错很难查（数据错了但没人报错）。轮询慢一点，但每一步状态标志都看得见 |
| **FatFs** | 独立的一大块，下一步 |
| **写操作** | 同上 —— 没有文件系统，写扇区的意义有限 |

---

## 八、下一步（加 FatFs 的路线）

```
1. 拿 FatFs 源码（ff.c / ff.h / diskio.c / ffconf.h）
     - Elm-Chan 官方（R0.15）
     - 或野火 SDK（但这份资料里我确认过没有）
2. 改 diskio.c：
     disk_initialize -> Sd_Init()
     disk_read       -> Sd_ReadBlocks()
     disk_write      -> 需要先实现 Sd_WriteBlocks()
     disk_ioctl      -> CTRL_SYNC / GET_SECTOR_COUNT / GET_SECTOR_SIZE
3. f_mount(&fs, "0:", 1)
4. f_open(&fp, "0:/log.md", FA_CREATE_ALWAYS | FA_WRITE | FA_READ)
5. f_write(...) + f_sync() 循环
```

**最接近可用的一步是 `disk_write`** —— 需要补 `Sd_WriteBlocks()`（CMD24 单块写 / CMD25 多块写 + CMD13 等忙）。

---

## 九、验证状态

| 项 | 状态 |
|---|---|
| Keil 编译 | ✅ **0 Error 0 Warning** |
| 逻辑审查 | ✅ 修掉了两个会导致必然失败的坑（CRC、提速断电）|
| **上板实测** | ❌ **没有** —— 我这边没有板子和卡，插卡识别成功与否需要你烧进去看 |

如果 `[1] init` 就失败，先按这个顺序查：

```
1. 卡插到位了吗？（J28 卡座有弹簧，要按到底"咔"一下）
2. 卡是不是 FAT32？>32GB 的卡默认可能是 exFAT，FatFs 要额外配置
3. 是不是坏卡？（换一张手机里用过的小容量卡试试）
4. 卡座触点脏了？（断电用橡皮擦一下金手指）
```
