# ZhaoBuyan — 嵌入式学习作品集

从 STM32F429 裸机起步 → FreeRTOS → 嵌入式 Linux 驱动 / BSP 方向的学习记录。

## 开发环境

| 项目 | 内容 |
|---|---|
| 开发板 | STM32F429IGT6（Cortex-M4F @180MHz，1MB Flash，256KB SRAM） |
| 工具链 | STM32CubeMX + Keil MDK5 + ST-Link |
| 调试串口 | USART1 @115200-8-N-1 |

## 目录结构

```
stm32-to-linux/
└── STM32F429/
    ├── LED_Key_Control/   # RGB 三色灯 + 按键，寄存器级 GPIO 操作
    ├── light0.0.1/        # 早期测试工程（保留记录）
    └── _archive/          # 旧备份（不提交）
```

## 学习进度

| 内容 | 说明 | 状态 |
|---|---|---|
| GPIO 寄存器操作 | 直接写 `GPIOx->MODER` / `BSRR`，理解推挽输出与共阳极 LED 驱动 | ✅ |
| 非阻塞三色循环 | `HAL_GetTick()` 时间戳 + 状态机，不用 `HAL_Delay` | ✅ |
| 按键扫描 | 轮询 + 软件消抖（无阻塞） | 进行中 |
| EXTI / 定时器 / UART / SPI / I2C / DMA | 后续阶段 | ⬜ |
| FreeRTOS | 后续阶段 | ⬜ |

## 笔记约定

每个实验目录下的 `README.md` 记录：**做了什么 / 现象 / 遇到的问题 / 解决过程**。
