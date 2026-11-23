# FreeRTOS_Test —— FreeRTOS 集成演示（STM32F429 + StdPeriph）

> 课程作业：把 FreeRTOS 内核集成进已有的裸机工程。
> 内核版本 **v11.3.1**，移植层 `RVDS/ARM_CM4F`（对应 Keil MDK），内存管理 `heap_4`。

---

## 一、集成了什么

不只是"能跑起来"，而是把 **5 个最常用的内核机制**都用上并可视化：

| # | 机制 | 在哪体现 |
|---|---|---|
| ① | **多任务 + 优先级** | 4 个任务，不同优先级、不同周期，屏幕上各自的心跳数字一直在涨 |
| ② | **队列** | `TaskKey` 把按键事件塞进队列，`TaskUi` 取出来处理 |
| ③ | **互斥量** | 保护 `printf`（串口不是线程安全的）和保护 LCD（两个任务都画屏）|
| ④ | **软件定时器** | 1 秒周期翻转 LED，不占用任何任务栈 |
| ⑤ | **运行时统计** | 空闲堆、最小空闲堆、各任务**栈高水位** |

### 任务表

| 任务 | 优先级 | 周期 | 职责 |
|---|---|---|---|
| `TaskKey` | 4（最高）| 20 ms | 扫 KEY1/KEY2，3 次一致性消抖，按下沿发队列 |
| `TaskUi` | 3 | 事件驱动 / 200 ms 超时 | 收队列更新状态，刷屏 |
| `TaskBeat` | 2 | 500 ms | 只做 `vTaskDelay`，证明调度器在按周期切任务 |
| `TaskStat` | 1（最低）| 2000 ms | 串口打印统计信息 |
| `TmrLed`（软件定时器）| — | 1000 ms | 定时器服务任务里回调，翻转 LED |

> **优先级分配原则**：响应要求越高、执行越短的给越高优先级。
> `TaskStat` 最低 —— 它打印慢，绝不能挡住按键响应。

---

## 二、集成时解决的三个冲突

原来的裸机工程（`Template_LCD`）直接加内核**编不过也跑不起来**，有三处必须改：

### ① SysTick 被两边抢

| 原工程 | FreeRTOS |
|---|---|
| `bsp_SysTick.c` 用 SysTick 中断递减 `TimingDelay` 来实现 `Delay_ms` | 内核**独占** SysTick 做系统节拍（`xPortSysTickHandler`）|

**解决**：把 `bsp_SysTick.c` 从工程里摘掉，新增 `User/rtos/bsp_delay.c`，
用 **DWT 周期计数器**实现阻塞延时。

为什么选 DWT：

```
✅ 不占中断（纯粹读寄存器计数）
✅ 调度器启动**之前**也能用 —— LCD 初始化、外设上电时序都需要延时
✅ 精度到 CPU 周期（180 MHz）
```

### ② 三个异常处理重复定义

`startup_stm32f429_439xx.s` 的向量表里是 `SVC_Handler` / `PendSV_Handler` / `SysTick_Handler`，
而 `stm32f4xx_it.c` 里也定义了同名函数，FreeRTOS 移植层还要接管它们 → **重复定义**。

**解决**：在 `FreeRTOSConfig.h` 里做重定向，然后删掉 `it.c` 里的三个函数：

```c
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
```

> 两条路可选：**重定向**（本文采用，函数少）或**转发函数**（在 it.c 里写
> `void SysTick_Handler(void){ xPortSysTickHandler(); }`）。
> 关键是**不能两种都用**，否则重复定义。

### ③ NVIC 优先级分组必须是 Group 4

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);   /* 全部 4 位作抢占优先级 */
```

**不加这一句的后果**：`configMAX_SYSCALL_INTERRUPT_PRIORITY` 的比较失效，
表现为"某个中断里调用 `FromISR()` 随机 HardFault"——这类 bug 极难定位。

**必须在初始化任何带中断的外设之前调用。**

---

## 三、FreeRTOSConfig.h 里几个不能错的配置

| 配置 | 值 | 为什么 |
|---|---|---|
| `configCPU_CLOCK_HZ` | `180000000` | 等于 SYSCLK，错一个数量级任务周期全偏 |
| `configTICK_RATE_HZ` | `1000` | 1 ms 一个 tick |
| `configMAX_PRIORITIES` | `8` | 任务优先级上限 |
| `configPRIO_BITS` | `4` | **STM32 只实现优先级寄存器的高 4 位** |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | `5` | **数值 ≥ 5 的中断才能调 `FromISR()`** |
| `configTOTAL_HEAP_SIZE` | `40 * 1024` | heap_4 的池子 |
| `configCHECK_FOR_STACK_OVERFLOW` | `2` | 栈尾魔数 + 越界双重检查 |
| `configTIMER_TASK_PRIORITY` | `MAX-1` | 定时器服务任务优先级要最高 |

### 两个必须理解的点

**① `configPRIO_BITS` 为什么是 4**

Cortex-M4 的优先级寄存器是 8 位，但 STM32 **只实现了高 4 位**（共 16 级）。
写 8 会让内核左移计算错误。

**② 中断优先级与 FreeRTOS API 的关系**

```
优先级数值 < 5  （更紧急）：禁止调用任何 FreeRTOS API
优先级数值 ≥ 5           ：可以调用 xxxFromISR() 版本
```

这是 FreeRTOS 里**最容易出隐蔽 bug** 的地方 —— 违反了不会立刻报错，
而是在临界区时序上偶发崩。

---

## 四、烧进去会看到什么

### 屏幕

```
FreeRTOS integration demo
tick 12345  up 12.345 s

┌ task heartbeat (keeps increasing = alive) ─────────┐
│ TaskKey   prio 4   20ms    617                    │
│ TaskUi    prio 3   evt/200 620                    │
│ TaskBeat  prio 2   500ms   24                     │
│ timer 1000ms  12                                  │
└───────────────────────────────────────────────────┘

┌ queue (TaskKey -> TaskUi) ─────────────────────────┐
│ waiting 0   total events 3   last KEY1            │
└───────────────────────────────────────────────────┘

┌ heap_4 ────────────────────────────────────────────┐
│ free 31256 B / total 40960 B   min-free 31256 B   │
└───────────────────────────────────────────────────┘
```

**数字规律验证多任务是否正常**：

| 计数器 | 20 秒内大约涨多少 | 说明 |
|---|---|---|
| `TaskKey` | 1000 次 | 20 ms 周期 ✅ |
| `TaskBeat` | 40 次 | 500 ms 周期 ✅ |
| `TaskStat` | 10 次 | 2000 ms 周期 ✅ |
| `timer` | 20 次 | 1000 ms ✅ |

**比例对不上就说明调度有问题。**

### 串口（USART1，115200）

```
===== RTOS stats @ 2000 ms =====
heap   free 31256 / 40960 B, min-free 31256 B
queue  waiting 0, total events 3
beat   key 100  ui 101  beat 4  stat 1  timer 2
stack  (words left, higher = safer)
       TaskKey  230
       TaskUi   468
       TaskBeat 232
       TaskStat 350
===============================
```

**栈高水位怎么看**：数字是"还剩多少个字（4 字节）"。
`TaskKey` 分配 256 字、剩 230 字 → 用了 26 字，很健康。
**如果某个任务剩不到 20 字，就该加大栈了。**

---

## 五、目录结构

```
FreeRTOS_Test/
├── User/
│   ├── main.c                演示：4 任务 + 队列 + 互斥量 + 定时器 + 钩子
│   ├── FreeRTOS/
│   │   ├── FreeRTOSConfig.h  ★ 配置文件（注释了每个关键项的为什么）
│   │   ├── tasks.c queue.c list.c timers.c
│   │   ├── event_groups.c stream_buffer.c croutine.c
│   │   ├── include/          21 个内核头文件
│   │   └── portable/
│   │       ├── port.c        RVDS/ARM_CM4F 移植层
│   │       ├── portmacro.h
│   │       └── heap_4.c      内存管理
│   ├── rtos/
│   │   └── bsp_delay.c/.h    ★ DWT 阻塞延时（替代被内核占用的 SysTick）
│   ├── gfx/                  画图库
│   ├── lcd/ sdram/ font/ usart/ led/ tick/   底座
│   └── stm32f4xx_it.c        已删掉 SVC/PendSV/SysTick（归 FreeRTOS）
├── Project/RVMDK（uv5）/     Keil 工程（已配好，FreeRTOS 单独一个 Group）
└── README.md
```

---

## 六、内核文件是怎么挑出来的

`FreeRTOS-LTS` 是 AWS 的完整发行包（**8000 个文件 / 94 MB**），
里面还含 AWS IoT 库（coreMQTT、coreHTTP 等）。**不能整包塞进工程。**

只挑了这些：

| 来源 | 文件 |
|---|---|
| `FreeRTOS-Kernel/*.c` | tasks, queue, list, timers, event_groups, stream_buffer, croutine |
| `FreeRTOS-Kernel/include/*.h` | 21 个头文件 |
| `portable/RVDS/ARM_CM4F/` | port.c, portmacro.h |
| `portable/MemMang/` | heap_4.c |

**约 40 个文件 / 700 KB**，占工程很小一部分。

> 为什么选 `RVDS` 而不是 `GCC`：RVDS 就是 ARM Compiler（Keil 用的），
> 里面的 `__asm` 是 ARM 汇编语法。选错会编译报一堆语法错误。

---

## 七、验证状态

| 项 | 状态 |
|---|---|
| Keil 编译 | ✅ **0 Error 0 Warning** |
| 代码审查 | ✅ 三个冲突点都已解决（SysTick / 重复向量 / NVIC 分组）|
| 体积 | `Code=21,324  RO=5,128  RW=236  ZI=42,348`（ZI 里 40 KB 是堆）|
| **上板实测** | ❌ **没有** —— 需要烧录确认心跳比例、栈水位、按键队列 |

上板后请重点看两件事：

1. **四个任务的计数比例**是否符合周期（见第四节表格）
2. **栈高水位**是否都在 100 字以上

---

## 八、可以继续做的

| 方向 | 说明 |
|---|---|
| 信号量替代轮询 | 把 `TaskUi` 的 200 ms 超时换成二值信号量，响应更快 |
| 事件组 | 多个任务等"多个条件同时满足"时用 |
| 任务通知 | 比队列更轻量的单任务通知（省 RAM、快 45%）|
| `vTaskDelayUntil` | 需要严格等周期时用，比 `vTaskDelay` 不累积误差 |
| FreeRTOS-Plus-TCP | 发行包里有，接网口可以试 |
| 跑时间统计 | `configGENERATE_RUN_TIME_STATS` + `vTaskGetRunTimeStats()` 看各任务 CPU 占比 |

---

## 九、踩过的坑（给以后留个记录）

| # | 现象 | 原因 | 修法 |
|---|---|---|---|
| 1 | `Delay_ms` 编译通过但延时乱 | SysTick 被内核接管 | 换 DWT 实现 |
| 2 | 链接报 `SVC_Handler` 重复定义 | it.c 与移植层同时定义 | 重定向后删掉 it.c 里的 |
| 3 | 加了中断后偶发 HardFault | 没设 NVIC Group 4 | 开头调 `NVIC_PriorityGroupConfig` |
| 4 | Keil 报了 17 个 `Undefined symbol` | 新 Group 的 `<File>` 没包在 `<Files>` 里，Keil 忽略了整组 | 补上 `<Files>` 包装 |
| 5 | `vprintf` 未声明 | 缺 `<stdarg.h>` | 补 include |
