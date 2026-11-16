# GPS_Test —— GPS NMEA 原始数据回显

> **这一步不解析，只把收到的原始 NMEA 打到串口上。**
> 目的：确认接线、波特率、数据真实性。

对应任务书 M2.1 的第一条：

> 「先用 USB-TTL 或 MCU 串口**原样打印/记录** GPS 原始 NMEA；
> 确认数据源真实、波特率正确，再做结构化解析。」

---

## 一、接线

| GPS 模块 | STM32F429 | 说明 |
|---|---|---|
| **TX** | **PB11**（USART3_RX）| **必须接** —— 数据从这里进 MCU |
| **RX** | PB10（USART3_TX）| 可选。只收数据时不用接 |
| **VCC** | 3.3V 或 5V | **看模块标注**。多数模块支持 3.3~5V |
| **GND** | GND | **必须接**，共地 |

### 为什么选 USART3 / PB10-PB11

这个工程基于 `Template_LCD`，已被占用的引脚：

```
LTDC（屏）     PA3/PA11/PA12/PB0/PB1/PB8/PB9/PC0/PC7/PD0/PD1/PD6/PE0-PE15/PF0-PF5/PF10-PF15/PG0-PG2/PG4-PG8/PG10-PG12/PG15/PH2-PH8/PH10-PH13/PH15/PI0/PI2/PI4/PI8-PI10
FMC（SDRAM）   同上（与 LTDC 共用大量引脚）
USART1（调试）  PA9 / PA10        ← 不能用
I2C1（MPU）     PB6 / PB7
SPI5（Flash）   PF6 / PF7 / PF8 / PF9
触摸            PH4 / PH5（GT911）+ PI8 / PD13
按键            PA0 / PC13
LED             PH10 / PH11 / PH12
```

**PB10 / PB11 空闲**，而且不和其他外设冲突 → 用 USART3。

---

## 二、烧进去会看到什么

### 上电自动找波特率

串口助手（USART1，**115200 8-N-1**）里会看到：

```
=====================================
 GPS NMEA raw echo  (USART3 PB10/PB11)
=====================================

--- probing 9600 baud ---
    first line: $GNRMC,081836.00,A,3150.70613,N,11711.90887,E,0.021,,060226,,,A*5E
    214 byte(s), 1 valid line(s) -> LOCKED

*** LOCKED at 9600 baud ***
$GNRMC,081836.00,A,...
$GPGGA,081836.00,...
...
```

搜索顺序：`9600 → 115200 → 4800 → 38400 → 57600 → 19200`，每个听 1.4 秒。
知道模块型号的话，把想用的放 `k_baud_list[]` 第一个就能省 2 秒。

### 屏幕显示

```
GPS NMEA raw echo                    USART3 PB10/PB11
BAUD 9600            LOCKED
bytes  1543        lines  21

last line:
┌──────────────────────────────────────────────┐
│ $GNRMC,081836.00,A,3150.70613,N,11711.9...   │
└──────────────────────────────────────────────┘
OK. raw lines echoed on USART1 @115200
wiring:  GPS TX -> PB11 (USART3_RX)
...
```

### 没数据时

```
*** NO DATA on any tested baud ***
check: GPS VCC+GND, GPS TX -> PB11, module LED blinking?
```

屏幕状态变红，每 3 秒打一次 `[no data for 3s]`。

---

## 三、怎么判断"通了"

**看到以 `$` 开头的行就通了。** 典型的两种：

```
$GNRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed,course,ddmmyy,,,A*CS
$GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,fix,sats,hdop,alt,M,...
```

### 关键字段速查（RMC）

| 位置 | 含义 | 要注意 |
|---|---|---|
| `$GNRMC` | talker=`GN`（多星座），`GP`=`GPS` | **`GN`/`GP`/`BD` 都会出现，解析要兼容** |
| 第 2 段 | UTC 时间 `hhmmss.ss` | — |
| 第 3 段 | **`A` = 有效 / `V` = 无效** | **`V` 时纬度经度全是空的，不能拿去算里程** |
| 第 4-5 段 | 纬度 `ddmm.mmmm` + `N/S` | 是**度分**，不是十进制度 |
| 第 6-7 段 | 经度 `dddmm.mmmm` + `E/W` | 同上 |
| 第 8 段 | **地面速度，单位「节」** | `km/h = 节 × 1.852` |
| 最后 `*CS` | 校验和 | `$` 和 `*` 之间所有字节异或 |

> **在室内多半是 `V`（无定位）**，或者空字段。这是正常的 ——
> 室内测的是"数据链路通不通"，不是"能不能定位"。
> 真要定位得拿到窗边或室外，等几十秒冷启动。

---

## 四、PC 端单元测试（不用板子）

组行逻辑抽在 `User/gps/gps_line.c`，纯 C、不依赖硬件，可以直接测：

```powershell
gcc -std=c11 -Wall -Wextra -IUser/gps tests_gps_line.c User/gps/gps_line.c -o tests_gps_line.exe
.\tests_gps_line.exe
```

覆盖 22 个断言：

```
CRLF / LF / CR 三种行尾
连续多行
半行不产出
分裂行重新拼接
空行忽略
超长行 -> 返回 -1、不产出、计数 +1
超长行的尾巴不能粘到下一行开头   ← 这条是写测试时发现的真 bug
恰好放满 max-1 个字符
```

当前结果：**22 passed, 0 failed**

---

## 五、目录

```
GPS_Test/
├── Project/RVMDK（uv5）/     Keil 工程（已配好）
├── Libraries/                ST 标准外设库
├── User/
│   ├── main.c                自动找波特率 + 收字节 + 打印 + 屏幕状态
│   ├── gps/
│   │   ├── gps_uart.c/.h     USART3 + 中断 + 环形缓冲（硬件相关）
│   │   └── gps_line.c/.h     组行器（纯 C，可 PC 单测）
│   ├── gfx/                  画图库（屏幕状态用）
│   ├── tick/  sdram/  lcd/   底座
│   └── ...
├── tests_gps_line.c          PC 单元测试
└── README.md
```

**分工原则**：硬件相关的（串口、中断、环形缓冲）和硬件无关的（组行）分开。
后者能在 PC 上测，前者只能上板测 —— 把能测的尽量挪到能测的那一侧。

---

## 六、下一步（还没做）

| 项 | 说明 |
|---|---|
| `nmea_checksum_ok()` | 校验 `*CS`，同样做成纯 C + 单测 |
| `rmc_parse()` | 拆字段、度分转十进制度、节转 km/h、判定 `A`/`V` |
| `gps_fix_update()` | 记录 `valid` / `lat` / `lon` / `speed` / `utc` / `last_rx_ms`，处理 3 秒超时 |
| 定位状态子状态机 | `NO_DATA / FIX_INVALID / FIX_VALID / STALE`（任务书 §4.1）|

**本工程只做到"看到原始数据"为止** —— 这正是任务书要求的第一步。
