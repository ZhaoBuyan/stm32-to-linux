# IAP_OTA —— 固件维护与远程升级系统

> 项目编号 **IAP-F429-2026-01**
> 平台：野火 STM32F429 挑战者 V2（STM32F429IGT6）· Keil MDK5 · CubeMX/CubeF4 HAL
> 当前进度：**阶段 A 完成，核心纯逻辑已单测通过**

---

## 一、这个项目要做什么

把裸机程序改造成**可维护的产品固件**：

```
PC 升级工具 ──USART/YMODEM──► Bootloader（接收/擦写/校验/跳转）──► APP v1.0 / v1.1
```

- 上电正常进 APP
- 能进独立的 Bootloader，通过串口升级 APP
- **升级失败也能保留维护入口**（不进坏 APP）
- 选做：ESP8266 Wi-Fi 取固件 → W25Q256 暂存 → 重启安装

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
> 详细的扇区数学见 [docs/阶段A_架构评审.md](docs/阶段A_架构评审.md)。

---

## 三、当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| A | 架构评审（分区/启动流程/模块划分/故障清单） | ✅ 完成 |
| A' | 纯 C 核心 + PC 单测 | ✅ **89 项通过** |
| B | Bootloader + APP 双工程、跳转 | ⬜ 待做 |
| C | 串口 IAP + P0 测试矩阵 | ⬜ 待做 |
| D | P1 Wi-Fi OTA | ⬜ 选做 |
| E | 验收材料 | ⬜ 待做 |

---

## 四、已完成的部分

### 4.1 纯 C 核心（`core/`）

四块逻辑**完全不依赖硬件**，所以在 PC 上就能验证完：

| 文件 | 职责 | 关键点 |
|---|---|---|
| `crc32.c/h` | CRC-32（zlib 兼容）| 半字节查表，省 Flash；标准向量 `0xCBF43926` |
| `app_layout.c/h` | 分区常量 + MSP/入口/范围判断 | Thumb 位、SRAM 范围、溢出、零长度 |
| `manifest.c/h` | 32 字节清单解析与校验 | 手工按偏移读写，不依赖结构体对齐/字节序 |
| `ymodem.c/h` | YMODEM 接收状态机 | 逐字节喂入；重复包 ACK 不重写；超长即中止 |

### 4.2 单测结果（`tests/tests_core.c`）

```
gcc -std=c11 -Wall -Wextra -Icore tests/tests_core.c `
    core/crc32.c core/app_layout.c core/manifest.c core/ymodem.c -o tests_core.exe
.\tests_core.exe
```

**89 passed, 0 failed**

覆盖：

```
CRC32        3 个标准向量 + 增量与一次性一致 + NULL 安全
分区/入口    MSP 9 种情况、入口 5 种、综合判断 6 种、擦写范围 7 种
Manifest     正常构建解析 + 8 种错误 + 版本字符串 + flags
YMODEM       正常流程(头包/128B/1024B/EOT)、重复包、坏 CRC、坏序号、
             CAN 取消、超 max_size 中止、垃圾字节忽略、CRC16 已知值
```

---

## 五、几个刻意的设计决定

### ① 纯 C 与硬件严格分界

```
能 PC 单测：crc32 / app_layout / manifest / ymodem      ← 已全部验证
只能上板：  flash_if（HAL Flash）/ boot_jump（寄存器+汇编）
```

**把能测的挪到能测的一侧**，剩下的才需要板子。这是前面几个工程一路沿用的做法。

### ② YMODEM 做成"逐字节喂入"的状态机

不阻塞、不等超时，调用方拿到 `YmReply` 决定发什么：

```c
switch (Ym_Feed(&ctx, byte)) {
case YM_REPLY_ASK_C: send('C');  break;
case YM_REPLY_ACK:   send(0x06); break;
case YM_REPLY_NAK:   send(0x15); break;
case YM_REPLY_ABORT: send_cancel(); break;
case YM_REPLY_DONE:  done = 1;    break;
}
```

好处：**PC 上可以用普通字节数组喂它**，把协议逻辑测干净，不依赖串口。

### ③ 三个容易做错的地方都处理了

| 坑 | 处理 |
|---|---|
| **重复包**（ACK 丢失后对端重发）| seq == 上一包时回 ACK 但**不重复写**，否则数据错位 |
| **写穿 APP 区** | 累计写入量超 `max_size` 立刻 ABORT（任务书第 23 行安全红线）|
| **CRC16 范围** | 只覆盖数据段，不含 SOH/seq/~seq |

---

## 六、下一步（阶段 B）

1. 用 **STM32CubeProgrammer** 读 `DB1M`，确认扇区表和上表一致
2. 从 HAL 工程复制出 **Bootloader** 和 **APP** 两个工程
3. APP 改 Keil `IROM1` 为 `0x08020000 / 0xE0000`，并在 `main()` 开头设 `SCB->VTOR`
4. 写 `flash_if.c` 与 `boot_jump.c`
5. 跑通"Bootloader → APP"，再故意烧坏 APP 首字验证拒绝跳转

---

## 七、安全红线（任务书第 23 行）

```
✗ 不擦 Bootloader 区（Sector 0-4）
✗ 不擦 Option Bytes
✗ 不擦未知分区
✗ 严禁把外部包里的地址直接当 Flash 写地址
```

代码落实：所有擦写经 `flash_if.c`，入口先做 `App_IsRangeInApp()` 检查；
目标地址**永远由 offset 累加得出**，绝不从包里读地址。

---

## 八、目录

```
IAP_OTA/
├── core/                  纯 C 核心（已单测）
│   ├── crc32.c/h
│   ├── app_layout.c/h
│   ├── manifest.c/h
│   └── ymodem.c/h
├── tests/
│   └── tests_core.c       89 项 PC 单测
├── docs/
│   └── 阶段A_架构评审.md   分区数学、启动流程、模块划分、故障清单
└── README.md
```

> Bootloader / APP 两个 Keil 工程在阶段 B 建，届时放在 `bootloader/` 和 `app/`。
