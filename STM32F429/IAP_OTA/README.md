# IAP_OTA —— 固件维护与远程升级系统

> 项目编号 **IAP-F429-2026-01**
> 平台：野火 STM32F429 挑战者 V2（STM32F429IGT6）· Keil MDK5 · CubeF4 HAL
> 当前进度：**阶段 A / B 完成**，两个工程均可编译，产物已生成；上板测试待做

---

## 一、这个项目做什么

把裸机程序改造成**可维护的产品固件**：

```
PC 升级工具 ──USART/YMODEM──► Bootloader（接收/擦写/校验/跳转）──► APP v1.0 / v1.1
```

- 上电正常进 APP
- 能进独立的 Bootloader，通过串口升级 APP
- **升级失败也保留维护入口**（不进坏 APP）
- 选做：ESP8266 Wi-Fi 取固件 → W25Q256 暂存 → 重启安装（P1，未做）

---

## 二、Flash 分区（DB1M = 0，单 Bank）

```
0x08000000 ┌──────────────────────┐
           │  Bootloader  128 KB  │  Sector 0-4  (16K×4 + 64K)
0x08020000 ├──────────────────────┤
           │  APP         896 KB  │  Sector 5-11 (128K×7)
0x080FFFFF └──────────────────────┘
```

> ⚠️ **DB1M=1（双 Bank）时扇区表完全不同**，必须先用 STM32CubeProgrammer 验证。
> 扇区数学与对比表见 [docs/阶段A_架构评审.md](docs/阶段A_架构评审.md)。

---

## 三、进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| A | 架构评审 + 纯 C 核心 + 89 项单测 | ✅ |
| **B** | **双工程 / flash_if / boot_jump / 版本差异 / 打包工具 / 互验测试** | ✅ |
| C | 上板跑 P0 测试矩阵 T01~T09 | ⬜ 待做 |
| D | P1 Wi-Fi OTA | ⬜ 选做 |
| E | 验收材料 | ⬜ 待做 |

---

## 四、目录

```
IAP_OTA/
├── core/                      纯 C 核心（零硬件依赖，已单测）
│   ├── crc32.c/h              CRC-32 zlib 兼容
│   ├── app_layout.c/h         分区 + MSP/入口/范围判断
│   ├── manifest.c/h           32 字节清单解析与校验
│   └── ymodem.c/h             YMODEM 接收状态机
│
├── bootloader/                Keil 工程（HAL）@ 0x08000000
│   ├── Core/Src/
│   │   ├── main.c             启动决策 + YMODEM 升级循环
│   │   ├── flash_if.c         所有擦写的唯一入口（范围检查在这里）
│   │   └── boot_jump.c        跳转前清理 + 汇编跳转
│   └── MDK-ARM/Bootloader.uvprojx    IROM1 = 0x08000000 / 0x20000
│
├── app/                       Keil 工程（HAL）@ 0x08020000
│   ├── Core/Src/
│   │   ├── main.c             业务 + 维护请求 + SCB->VTOR
│   │   └── version.c          版本号 / 构建时间 / 故障计数
│   └── MDK-ARM/App.uvprojx         IROM1 = 0x08020000 / 0xE0000
│
├── tests/
│   ├── tests_core.c           89 项：CRC32 / 分区 / Manifest / YMODEM
│   └── tests_package.c        8 项：PC(Python) 与 MCU(C) 的 CRC 互验
│
├── tools/
│   ├── make_package.py        打包：BIN -> Manifest + .pkg + 校验信息
│   └── build_versions.py      一次编出 v1.0 / v1.1 两个版本
│
├── dist/                      ★ 交付产物
│   ├── bootloader.bin    16,892 B
│   ├── Bootloader.hex    47,563 B
│   ├── app_v1.0.bin      13,240 B     sha256(前16) 4af95ca0233db224
│   ├── app_v1.1.bin      13,252 B     sha256(前16) 0a44ff24ce279fbf
│   └── *.man / *.pkg / *.txt          清单、整包、人读校验信息
│
└── docs/阶段A_架构评审.md
```

---

## 五、两个工程的编译配置

| 项 | Bootloader | APP |
|---|---|---|
| IROM1 起始 | `0x08000000` | **`0x08020000`** |
| IROM1 大小 | `0x20000`（128 KB）| `0xE0000`（896 KB）|
| `VECT_TAB_OFFSET` | `0x00000000U` | **`0x00020000U`** |
| `SCB->VTOR` | — | `main()` 第一句显式设置 |
| 编译体积 | Code 15,660 B | Code 11,962 B |

**烧录顺序**：先 `Bootloader.hex`（ST-LINK 烧一次），之后 APP 只能通过串口 YMODEM 升级。

> ⚠️ 这两个数字（IROM1 的 0x08020000 和 VECT_TAB_OFFSET 的 0x20000）**必须同时改**。
> 只改一个的后果：APP 能跑，但**所有中断都跳到 Bootloader 的向量表**，
> 表现为"串口收不到数据 / 定时器不响应"，而调试器看着一切正常。

---

## 六、三个进入维护模式的方式

| 方式 | 实现 |
|---|---|
| **上电按住 KEY1（PA0）** | Bootloader 开机有 1 秒窗口，按下就进维护 |
| **APP 里按 KEY2 / 发 'B'** | APP 写备份寄存器魔数 `0xB0071A70` 后软复位，Bootloader 读到就进维护 |
| **无有效 APP** | 自动停在 Bootloader（兜底）|

---

## 七、v1.0 与 v1.1 的差异（可观察）

| | v1.0 | v1.1 |
|---|---|---|
| 采样周期 | 500 ms | **200 ms** |
| LED 节奏 | 慢闪 | **快闪** |
| 串口上报 | `[APP v1.0.0] t=... env=...` | `[APP v1.1.0] t=... env=... faults=...` |
| 产物 | 13,240 B | 13,252 B |

**二进制都不同（sha256 前 16 位 `4af95ca0` vs `0a44ff24`）** —— 不是只改版本字符串。

编译：
```powershell
python tools\build_versions.py     # 自动编两版并各自另存
```

原理：`app/Core/Inc/version.h` 用宏 `APP_V11` 区分，脚本临时往 Keil 的 `Define` 里加/删该宏。

---

## 八、打包与互验（PC / MCU 必须一致）

```powershell
# 打包（生成 .man / .pkg / .txt 到 dist/）
python tools\make_package.py dist\app_v1.0.bin --ver 1.0.0 --out dist

# 只校验已有 .man
python tools\make_package.py dist\app_v1.0.bin --verify
```

**互验测试**（用和 MCU 完全相同的 C 代码解析 Python 生成的包）：

```powershell
gcc -std=c11 -Icore tests\tests_package.c core\crc32.c core\app_layout.c core\manifest.c -o t_pkg.exe
.\t_pkg.exe dist\app_v1.0.pkg
```

实测结果：

```
app_v1.0.pkg  image crc: manifest 0xAB438655 / C-calc 0xAB438655   PASS
app_v1.1.pkg  image crc: manifest 0xD6A5B77A / C-calc 0xD6A5B77A   PASS
10 passed, 0 failed
```

**这一步很关键**：Python 用 `zlib.crc32`，设备端用 `core/crc32.c`。
如果两边的初始值/异或/覆盖范围有任何差别，在 PC 上永远自洽，一上板就"校验失败"。

---

## 九、单元测试

```powershell
# 核心逻辑：89 项
gcc -std=c11 -Wall -Wextra -Icore tests\tests_core.c `
    core\crc32.c core\app_layout.c core\manifest.c core\ymodem.c -o tests_core.exe
.\tests_core.exe

# 包互验：10 项（需要 dist\*.pkg）
```

| 测试 | 覆盖 |
|---|---|
| `tests_core.c` | CRC32 标准向量 · MSP/Thumb/范围/溢出/零长度 · Manifest 正常+8 种错误 · YMODEM 正常流程/重复包/坏CRC/坏序号/CAN/超长 |
| `tests_package.c` | 清单可被 C 解析 · 字段自洽 · **C 与 Python 的 CRC32 逐位相同** · 镜像长度不超 APP 区 |

---

## 十、上板测试步骤（阶段 C）

```
1. ST-LINK 烧 Bootloader.hex（只烧这一次）
2. 复位 -> 串口应打印 Bootloader 横幅，然后跳转 APP
   （此时 APP 区是空的，会停在 "no valid APP, staying in bootloader"）
3. 串口工具（Xshell / Tera Term）以 YMODEM 发送 app_v1.0.bin
4. 复位 -> 应进 APP v1.0，500ms 一个采样点，LED 慢闪
5. 上电按 KEY1 -> 进 Bootloader -> YMODEM 发送 app_v1.1.bin
6. 复位 -> 应进 APP v1.1，200ms 一个采样点，LED 快闪，多出 faults 字段
7. 故障注入：
   - 传输中拔线            -> 不误判完成
   - 发一个截断的 bin      -> 向量校验失败，拒绝跳转
   - 人为改坏 bin 首字     -> 进 Bootloader 而不是崩
```

---

## 十一、设计上的几个硬约束（都落到代码里了）

| 约束 | 落实方式 |
|---|---|
| **不得擦 Bootloader 区** | 所有擦写经 `flash_if.c`，入口先过 `App_IsRangeInApp()`；擦除只擦 Sector 5-11 |
| **严禁把包内地址当写地址** | 调用方只传 **offset**，绝对地址在 `flash_if.c` 里由 `APP_ADDR + offset` 算出 |
| **写穿 APP 区** | YMODEM 累计写入量超 `max_size` 立刻 ABORT |
| **擦除时机** | 收到第一条**数据**才擦，误按按键不会毁掉正在跑的 APP |
| **跳转前清上下文** | 关中断 → 关 SysTick → 关外设 → 清 NVIC → 设 VTOR → 设 MSP |

---

## 十二、还没做的

| 项 | 说明 |
|---|---|
| **上板实测** | 本仓库所有验证都是编译 + PC 单测；两个工程**从未在板子上跑过** |
| P1 Wi-Fi OTA | ESP8266 + W25Q256 暂存 + OTA 状态持久化 |
| 断电恢复 | 安装阶段断电后的重新校验安装 |
| 签名校验 | T13（P2）|
| A/B 双区回滚 | 本方案不保存旧镜像，**不具备回滚能力**（任务书第 61 行明确区分了这两个概念）|
