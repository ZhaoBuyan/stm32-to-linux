# PCFbView —— 通用帧缓冲预览模拟器

> **打开模拟器 → 选项目文件夹 → 跑起来。**
>
> 不用烧录、不用接板子，在 Windows 上直接看见嵌入式项目画到屏幕上的东西。

它**不认识任何具体项目**。项目只要编一个几十行的「适配器 DLL」，
导出 5 个 `Pv_*` 函数，就能被它加载。

---

## 一、使用流程

```
1. 双击 fbview.exe            → 弹出文件夹选择框
2. 选中项目文件夹              → 自动找适配器
3. 窗口出现                    → WASD 模拟倾斜，1/2 代替按键，ESC 退出
```

也支持命令行：

```powershell
.\fbview.exe -dir <项目文件夹>
.\fbview.exe -dir <项目文件夹> -export frame.bmp    # 不开窗口，导出一帧
.\fbview.exe -dir <项目文件夹> -list                # 只检查能不能加载
```

### 键位

| 键 | 作用 |
|---|---|
| `W` `A` `S` `D` | 模拟倾斜（按住逐渐加大，松开回零）|
| `1` | KEY1（开始 / 暂停 / 继续 / 下一关 / 重试）|
| `2` | KEY2（重玩本关）|
| `ESC` | 退出 |

---

## 二、适配器怎么找

模拟器读了项目文件夹后，按这个顺序找适配器：

| 顺序 | 条件 |
|---|---|
| ① | `preview.ini` 里写了 `dll=`，且文件存在 → 直接加载 |
| ② | 文件夹里任意 `*adapter*.dll` → 加载 |
| ③ | `preview.ini` 里写了 `sources=` → **现场用 gcc 编译**成 `_fbview\adapter.dll` 再加载 |
| ④ | 都没有 → 弹窗提示需要什么 |

编好一次之后会留下 `_fbview\adapter.dll`，**之后就直接加载，不再重复编译**。
删掉 `_fbview` 文件夹即可强制重编。

---

## 三、`preview.ini`

```ini
width=800
height=480
fps=30
title=Tilt Game (PC preview)

; 优先加载这个 DLL
dll=game_adapter.dll

; 没有 DLL 时用它编译（相对本目录，逗号分隔）
sources=User\game\preview_adapter.c,User\game\pc_bsp_lcd.c,User\game\game.c,User\game\app.c,User\game\render.c

; 额外的 -I（相对本目录）
includes=User\game

; 额外的 -D
defines=PC_PREVIEW
```

**没有 `preview.ini` 也能用** —— 会按 800×480 / 30fps 去找 `*adapter*.dll`。

---

## 四、写一个适配器（约 60 行）

完整示例见 `demo_adapter.c`。

```c
#define FBVIEW_BUILD_DLL        /* 必须在 include 之前，否则不会导出 */
#include "fbview.h"

static const FvHost *s_host = NULL;

int Pv_Attach(const FvHost *host)     /* 拿到帧缓冲和宿主能力 */
{
    s_host = host;
    return 0;                          /* 非 0 = 放弃加载 */
}

void Pv_Update(float tilt_x, float tilt_y, float dt)   /* 推进一帧 */
{
    /* 你的物理/状态机 */
}

void Pv_Render(void)                   /* 把画面画进 s_host->fb */
{
    /* 往 s_host->fb 写 RGB565 像素；也可以调 s_host->draw_text 画字 */
}

void Pv_Key(int key) { }               /* 1 = KEY1，2 = KEY2 */

const char *Pv_Title(void)             /* 窗口标题，可返回 NULL */
{
    return "my project";
}
```

### 宿主给的东西（`FvHost`）

| 字段 | 说明 |
|---|---|
| `fb` | 帧缓冲，`uint16_t*`，RGB565，长度 `w*h` |
| `w`, `h` | 尺寸（由 `preview.ini` 决定）|
| `draw_text(s, x, y, c565)` | 用系统字体叠画一行文字（**不进帧缓冲**，所以导出的 BMP 里没有字）|
| `tilt_x`, `tilt_y` | 当前模拟倾斜量 |
| `fps` | 目标帧率 |

### 编译成 DLL

```powershell
gcc -std=c11 -O2 -shared -DFBVIEW_BUILD_DLL -I<PCFbView目录> -I<你的头文件目录> `
    -o <项目目录>\xxx_adapter.dll `
    <适配器.c> <你的其他.c...>
```

---

## 五、构建模拟器本体

```powershell
cd PCFbView
.\build.ps1              # 生成 fbview.exe + demo_adapter.dll
.\build.ps1 -HostOnly    # 只生成 fbview.exe
```

依赖：`C:\msys64\mingw64\bin\gcc.exe`（本机 gcc 16.1.0）+ Windows 自带的 gdi32/user32/shell32。

---

## 六、目录

```
PCFbView/
├── fbview.h          插件约定：FvHost + 5 个 Pv_* 函数
├── fbview_host.h     宿主内部接口（适配器不用看）
├── fbview.c          宿主核心：帧缓冲、窗口、输入、计时、文字、导出 BMP
├── fbview_loader.c   找/编译/加载适配器
├── fbview_main.c     入口：选文件夹 → 加载 → 跑
├── demo_adapter.c    示例适配器（棋盘格 + 键盘推方块）
└── build.ps1
```

---

## 七、踩过的坑

| # | 坑 | 现象 | 解法 |
|---|---|---|---|
| 1 | **gcc 的临时目录不能含中文** | `Cannot create temporary file ... Permission denied` | 宿主自己探测一个**纯 ASCII 且可写**的目录（`C:\pc_build` 等）设给 TMP/TEMP |
| 2 | **`MessageBoxA` 画中文乱码** | 弹窗一片问号 | 用 `MessageBoxW`，UTF-8 → UTF-16 转换 |
| 3 | **给子进程塞 `INVALID_HANDLE_VALUE` 当 stdin** | gcc 报奇怪的临时文件错误 | 改用 `cmd.exe /c ... > log 2>&1` 做重定向 |
| 4 | **`-DFBVIEW_BUILD_DLL` 和源码里重复定义** | 编译警告 | 源码里用 `#ifndef` 包住 |
| 5 | **`GetProcAddress` 转函数指针** | `-Wcast-function-type` 警告 | 加 `-Wno-cast-function-type`（这是该 API 的固有用法）|
| 6 | **`BITMAPINFO.biHeight` 为正** | 画面上下翻转 | 必须写 `-h`（负数）|

> ⚠️ 第 1 条在**受限环境**里可能无解：如果运行模拟器的进程无权在项目目录之外建目录，
> "现场编译"就会失败。此时先单独编好 `xxx_adapter.dll` 放进项目文件夹即可（流程 ①/②）。

---

## 八、它不适合做什么

| ❌ | 原因 |
|---|---|
| 验证 LTDC 时序 / 刷新率 / 撕裂 | 没有硬件模型 |
| 验证 SDRAM 带宽 | 同上（这是 V2 想加的）|
| 验证中断 / DMA / 触摸 | 同上 |
| 精确的传感器物理 | 倾斜是键盘模拟的，量纲靠 `TILT_MAX` 对齐 |

**它擅长的是**：调 UI 配色与排版、调手感、验证关卡设计、验证状态机流程 ——
以及**改一行代码 2 秒就能看到结果**（不用烧录）。

真需要硬件级验证，用 Renode（见仓库根目录 `嵌入式模拟器方案.md`）。
