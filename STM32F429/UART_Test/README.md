# UART_Test — 串口通信（USART1）

## 功能

- 板子通过 **USART1** 与 PC 通信
- 当前实现：**回环（Echo）** —— PC 发什么，板子原样发回
- 另有"心跳"：每秒主动发一次 `OK`，用于确认程序在运行

## 硬件配置

| 项目 | 值                                |
| ---- | --------------------------------- |
| 串口 | USART1                            |
| 引脚 | **PA9 = TX**，**PA10 = RX**       |
| 参数 | 115200 / 8 位 / 无校验 / 1 停止位 |

- 板子 USB 转串口（CH340）通过 **PA9/PA10 跳线帽**连到 MCU
- ⚠️ **调试器（DAP）和串口是两条独立通路，需要两根 USB 线**：
  - 调试器线 → 只负责下载 / 调试（**不传串口数据**）
  - 板子「USB 转串口」线 → 才是串口通信

## 现象

- 串口助手每秒收到 `OK`
- 发送 `abc` → 原样收到 `abc`（回环成功）

## 遇到的问题 & 解决

1. **串口收不到数据**
   - 设备管理器里有两个 COM 口，一开始选了**调试器的虚拟串口（COM8）**，它的 TX/RX 根本没接到 MCU
   - 解决：用「拔插法」确认板子的串口是 **COM3**，改用后正常

2. **下载后程序不运行**
   - 进过调试模式，退出时 CPU 停在 **halt（暂停）** 状态
   - 解决：按板子**复位键**恢复；并在 Keil
     `Options for Target → Debug → Settings → Flash Download` 勾上 **Reset and Run**

3. **教训**
   - 收不到数据，**先查连接（端口、线、跳线帽），再怀疑代码** —— 硬件/连接的假故障比真 bug 多得多

## 关键点

- `HAL_UART_Transmit()` —— 阻塞发送
- `HAL_UART_Receive(&huart, &buf, size, timeout)` —— **阻塞接收**，最多等 `timeout` 毫秒，**会卡住主循环**
- 调试技巧 **"心跳"**：让程序周期性打印，确认它还在运行

## 中断接收（HAL_UART_Receive_IT）

### 现象
- 回显正常，可连续收发

### 关键点
- 中断接收是【一次性】的：收完 1 个字节，接收中断会自动关闭
- 必须在 `HAL_UART_RxCpltCallback` 里【重新调用 `HAL_UART_Receive_IT`】才能继续收
- 验证实验：注释掉"重新启动"那行 → 发 `abc` 只收到 `a`，且必须复位才能再收

### 遇到的问题
- `rx_data` 定义为 main 的局部变量 → 回调里报 "identifier is undefined"
  → 改成全局变量解决

## DMA + 空闲中断接收不定长数据（最终版）

### 现象
- **主循环为空**，收发全部由 DMA + 中断完成
- 任意长度（含中文）都能原样回显

### 关键点
- **DMA**：专职数据搬运工，搬数据不占用 CPU
- **空闲中断（IDLE）**：总线上一段时间没有新数据 → 判定"一帧结束"
- `HAL_UARTEx_ReceiveToIdle_DMA(huart, buf, size)` + `HAL_UARTEx_RxEventCallback(huart, Size)`
  - 回调参数 `Size` = 本次实际收到的字节数（**不定长**也能处理）
- 每次收完必须【重新调用】`HAL_UARTEx_ReceiveToIdle_DMA`

### 三种发送方式对比

| 方式 | 谁搬数据 | CPU |
|---|---|---|
| `HAL_UART_Transmit` | CPU | 阻塞，搬完才返回 |
| `HAL_UART_Transmit_IT` | CPU（中断） | 每个字节被打断一次 |
| `HAL_UART_Transmit_DMA` | **DMA** | **立即返回** |

### 演进总结

| 版本 | 主循环里要做什么 |
|---|---|
| 阻塞 | `HAL_UART_Receive(..., 100)` → 卡住主循环 |
| 中断 | 重新 `HAL_UART_Receive_IT()` |
| **DMA + IDLE** | **什么都不用干** ✅ |

## 中断接收（HAL_UART_Receive_IT）

### 现象

- 回显正常，可连续收发

### 关键点

- 中断接收是【一次性】的：收完 1 个字节，接收中断会自动关闭
- 必须在 HAL_UART_RxCpltCallback 里【重新调用 HAL_UART_Receive_IT】才能继续收
- 验证实验：注释掉"重新启动"那行 → 发 "abc" 只收到 "a"，且必须复位才能再收
- rx_data 必须是【全局变量】（回调里要访问，局部变量看不见）

### 遇到的问题

- rx_data 定义为 main 的局部变量 → 回调里报 "identifier is undefined"
  → 改成全局变量解决
