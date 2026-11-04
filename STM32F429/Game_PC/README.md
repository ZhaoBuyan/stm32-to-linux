# PC 预览器（Game_PC）

> **不用烧录、不用接板子，在 Windows 上直接看见并玩这个游戏。**
>
> 改一行配色 → 跑 `build.ps1`（约 2 秒）→ 窗口里立刻看到。
> 取代原来「改 → F7 → F8 → 烧录 → 拍照」的 1~2 分钟循环。

---

## 快速开始

```powershell
cd STM32F429\Game_PC
.\build.ps1
.\game_preview.exe
```

| 键 | 作用 |
|---|---|
| `W` / `S` | 前后倾斜 |
| `A` / `D` | 左右倾斜 |
| `1` | **KEY1**（开始 / 暂停 / 继续 / 下一关 / 重试）|
| `2` | **KEY2**（重玩本关）|
| `ESC` | 退出 |

倾斜是「按住逐渐加大、松开回零」，模拟真板子的手感。

---

## 它是怎么做到的

**不模拟硬件，只复用我们自己的软件栈。**

```
        app.c    状态机 / 按钮            ← 一个字不改
          ↑
        game.c   物理 / 碰撞 / 12 关        ← 一个字不改
          ↓
       render.c  画到显存                 ← 只加 3 行条件编译
          ↓
   ┌──────┴───────┐
   ↓              ↓
真板子           PC 预览
写 0xD0000000    写 g_pc_fb 数组
（LTDC 扫描）     （GDI 贴到窗口）
```

`render.c` 里只有一个地方真正碰硬件 —— `fill()` 最终写到
`(uint8_t *)(LCD_FRAME_BUFFER + BUFFER_OFFSET)`。
把这个宏指向一块普通数组，**整条渲染链就跑在 PC 上了**。

### 唯一改动的 3 行

`Game_LCD/User/game/render.c` 顶部：

```c
#ifdef PC_PREVIEW
#include "pc_bsp_lcd.h"          /* PC 预览器：假显存 */
#else
#include "../lcd/bsp_lcd.h"      /* 真板子：野火驱动 */
#endif
```

固件不定义 `PC_PREVIEW`，走的还是原分支 —— **对板子零影响**（已验证 0 Error 0 Warning）。

---

## 文件说明

| 文件 | 作用 |
|---|---|
| `pc_bsp_lcd.h` | 假的板级 LCD 头文件：屏幕参数、假显存、`LCD_*` 接口 |
| `pc_bsp_lcd.c` | 假显存数组 + 文字收集（供 GDI 绘制）|
| `main_pc.c` | Win32 窗口、30fps 主循环、键盘模拟倾斜、GDI 贴图与画字 |
| `build.ps1` | 一条 gcc 命令 |
| `make_frame.exe` 的源码 `make_frame.c` | 离屏渲染工具（见下）|

---

## 离屏渲染工具（`make_frame.c`）

不开窗口，直接把几帧画面导成 BMP —— **方便在没有图形界面时检查布局**：

```powershell
gcc -std=c11 -O2 -DPC_PREVIEW -I. -o make_frame.exe `
    make_frame.c pc_bsp_lcd.c `
    "..\Game_LCD\User\game\game.c" `
    "..\Game_LCD\User\game\app.c" `
    "..\Game_LCD\User\game\render.c"

.\make_frame.exe f1_ready.bmp f2_running.bmp f3_win.bmp f4_fail.bmp
```

导出的四帧分别是：**READY / RUNNING / WIN / FAIL**。

> ⚠️ 导出的位图里**没有文字** —— 文字是走 GDI 在窗口上叠画的，
> 不进显存。这是设计如此，不是 bug。

---

## 几个踩过的坑（都在 `pc_bsp_lcd.h` 里注了）

| 坑 | 现象 | 原因 |
|---|---|---|
| **指针被截断** | 程序瞬间退出、无任何输出 | mingw64 默认编 **64 位**，`(uint32_t)g_pc_fb` 把地址截断 → `LCD_FRAME_BUFFER` 用 `uintptr_t` |
| **文字竖排** | 字从上往下排 | `LCD_DisplayChar` 参数是 **`(y, x)`**，不是 `(x, y)` |
| **画面上下翻转** | 图形倒过来 | `BITMAPINFO.biHeight` 必须是 **`-480`** |
| **gcc 建不了临时文件** | `Fatal error: can't create ...\Temp\ccXXX.o` | 中文路径，`build.ps1` 里已把 `$env:TMP` 指到 `C:\pc_build` |

---

## 能验证什么 / 不能验证什么

| ✅ 能 | ❌ 不能 |
|---|---|
| 配色、圆角、球的样式、横幅排版 | LTDC 时序、刷新率、撕裂 |
| 手感（加速度、摩擦、死区）| SDRAM 带宽 |
| 关卡设计（能不能过、有没有死角）| 触摸 / 中断 / DMA |
| 状态机、按钮流程 | 真实的传感器方向 |

> 这里的 `TILT_MAX = 6300` 是板子上**实测**的值
> （7 寸屏抬高一边时 `ax` 的偏移量），所以手感和真机接近。

---

## 后续可加（V2）

接入点已经预留：把 `fill()` 从"直接写显存"改成函数指针，
就能同时挂多个后端（板子 / PC / 带宽统计）。

- 屏幕类型选择（480×272 / 800×480、RGB565 / RGB888）
- **SDRAM 带宽模拟** —— 统计每帧写入字节，超阈值丢弃 → 复现"花屏"
- **传感器模型** —— 噪声 / 零偏 / 漂移 / 回放真实数据
- 调试叠加层（轨迹、碰撞框、倾角向量）
- 关卡编辑器（拖矩形，导出 C 代码）

详见仓库根目录 `嵌入式模拟器方案.md`。
