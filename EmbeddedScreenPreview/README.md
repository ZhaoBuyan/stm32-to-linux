# EmbeddedScreenPreview —— 通用帧缓冲预览模拟器

> **打开模拟器 → 选项目文件夹 → 跑起来。**（EmbeddedScreenPreview）
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
| **`F1`** | 开关调试叠加（帧率 / 帧耗时 / 分辨率 / 倾斜量）|
| **`F12`** | 截图，存到 `<exe目录>\screenshots\shot_NNNN.bmp` |
| **`空格`** | 暂停 / 继续（暂停时仍然渲染，只是不推进逻辑，方便看细节）|
| **`F11`** | 全屏 / 还原 |
| **`R`** | 重新加载适配器 —— **改完代码重编 DLL，按 R 立刻看到，不用关程序** |
| `ESC` | 退出 |

标题栏会实时显示帧率，例如：

```
LEVEL 3/12   RUNNING   12.3s   WALL 2   [30 fps]
```

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
| `draw_text(s, x, y, c565)` | 用系统字体叠画一行文字（在 RGB565 画面之后合成，截图和导出均包含文字）|
| `tilt_x`, `tilt_y` | 当前模拟倾斜量 |
| `fps` | 目标帧率 |

### 编译成 DLL

```powershell
gcc -std=c11 -O2 -shared -DFBVIEW_BUILD_DLL -I<EmbeddedScreenPreview目录> -I<你的头文件目录> `
    -o <项目目录>\xxx_adapter.dll `
    <适配器.c> <你的其他.c...>
```

---

## 五、构建模拟器本体

```powershell
cd EmbeddedScreenPreview
.\build.ps1              # 生成 fbview.exe + demo_adapter.dll + 游戏适配器
.\build.ps1 -HostOnly    # 只生成 fbview.exe
```

依赖：`C:\msys64\mingw64\bin\gcc.exe`（本机 gcc 16.1.0）+ Windows 自带的 gdi32/user32/shell32。

---

## 六、目录

```
EmbeddedScreenPreview/
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
| 测量真实 SDRAM 带宽 | 当前只有可调上限的帧级估算模型 |
| 验证中断 / DMA / 触摸 | 同上 |
| 精确的传感器物理 | 倾斜是键盘模拟的，量纲靠 `TILT_MAX` 对齐 |

**它擅长的是**：调 UI 配色与排版、调手感、验证关卡设计、验证状态机流程 ——
以及**改一行代码 2 秒就能看到结果**（不用烧录）。

真需要硬件级验证，用 Renode（见仓库根目录 `嵌入式模拟器方案.md`）。

## 运行脚本前必读：PowerShell 执行策略

Windows 默认策略（`RemoteSigned`）**不允许直接运行下载/新建的 .ps1**，会报
"未对文件进行数字签名"。两种解决办法：

```powershell
# 办法一：单次绕过（推荐，不改系统设置）
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\test.ps1

# 办法二：只对当前用户放开本地脚本
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```
## 稳定性验证

键盘输入仅在预览窗口处于前台时生效。标题栏帧率按包含帧间等待的实际周期计算；调试叠加中的毫秒数仍表示本帧处理耗时。

按 R 重载时会先卸载原 DLL。如果加载或初始化失败，程序输出错误并以非零状态退出，避免继续调用已卸载的代码。重编 DLL 后可重新打开预览器。

构建脚本会补齐 MinGW 的 PATH，并在完成后恢复 PATH、TMP、TEMP。

运行回归验证：

```powershell
cd EmbeddedScreenPreview
.\test.ps1
```

包括加载器连续重载及失败清理、窗口等比缩放计算、带宽临界值与恢复、不同尺寸项目切换及失败回退、游戏物理、状态机，以及 800×480 BMP 离屏导出。

## 大窗口、触屏与传感器

左上是等比缩放的 LCD 预览，左下是带宽统计和终端。终端显示宿主与适配器的 stdout/stderr，并将完整日志写入 `_fbview/session-<PID>.log`。

在 Game_LCD 中，鼠标点击预览内的 START、PAUSE 等按钮即可模拟触屏。点击黑边不会触发触屏。`A` 向左，`D` 向右，`W` 向上，`S` 向下。点击预览区可将键盘焦点从控件切回游戏。

右侧提供温度、湿度、光照滑条。切换到 Demo 可看到预览数值随滑条更新，温度达到 60 °C 时文字变红。Game_LCD 没有传感器输入接口，因此面板会禁用滑条。

新适配器可选实现 `Pv_Touch(int x, int y, int pressed)` 和 `Pv_SetSensor(int sensor, float value)`。触屏坐标使用原始 LCD 像素，`pressed` 为 1 表示按下或移动，0 表示释放，-1 表示取消。传感器编号和单位定义在 `fbview.h`，旧 DLL 仍可加载。

## 窗口、项目与带宽模拟

- 拖动边框、最大化或 F11 全屏：保持原始宽高比，空余区域留黑边，文字与画面一起缩放。实际帧缓冲尺寸保持项目配置值。
- 左上方下拉框切换项目；默认收录当前项目、自带 Demo 和 Game_LCD。`Add project...` 可添加其他文件夹，列表在当前进程内保留。切换会重置目标项目；加载失败保留当前项目。
- 可重复传入 `-dir` 预填项目列表，第一个项目作为启动项。
- 右上方选择 SDRAM 带宽上限，0 / unlimited 表示不限制。底部显示需求读带宽、估算写带宽、合计需求、模型实际服务带宽、缺失像素比例和出现 underrun 的宿主帧数。
- F12 和 `-export` 保存 32 位、从上往下排列的 BMP，包含文字和模拟条纹，尺寸与帧缓冲相同，不包含工具栏。

在项目 `preview.ini` 中可配置：

```ini
; 十进制 MB/s，0 表示无限；可在窗口中临时更改
bandwidth_mb_s=80
; 面板扫描频率，独立于 fps
scan_hz=60
; 每次 Render 写入的完整帧等效次数；适配器没有逐次访问计数时的估计
write_passes=1
```

默认 RGB565、800×480、游戏 30fps、面板 60Hz：

```
扫描读：800 × 480 × 2 × 60 = 46.08 MB/s
绘制写：800 × 480 × 2 × 30 × 1 = 23.04 MB/s（估算）
合计：69.12 MB/s
```

窗口中写带宽按实际宿主渲染频率估算，因此数值会随运行帧率变化；离屏导出使用配置 fps。空格暂停仍调用 Render，所以绘制写带宽仍存在。降低游戏 fps 不会降低面板的扫描读带宽。

模型把写请求优先分配到总带宽上限，剩余额度服务扫描读请求，按 64 字节 RGB565 读取块分配。取不到的块重复上一个有效像素，形成条纹；若写请求已经占满预算，扫描画面会变黑。恢复足够带宽后，下帧恢复正常。模型只影响最终显示副本，不改游戏显存和逻辑。**这不代表真实板子一定出现同样条纹，也没有逐周期模拟 FMC、缓存、消隐、突发延迟和 FIFO。**

带宽不足可能引发 LTDC FIFO underrun 的依据见 ST [AN4861](https://www.st.com/content/st_com/en/technical-documents/an4861-introduction-to-lcdtft-display-controller-ltdc-on-stm32-mcus-stmicroelectronics_asset/chunk-005-topic-an4861-creating-a-graphical-application-with-ltdc.html)。这里的扫描读公式只计算有效像素平均流量，适合演示阈值，不用于预测硬件极限。

```powershell
.\fbview.exe -dir . -dir ..\STM32F429\Game_LCD
.\fbview.exe -dir . -bandwidth 60 -export limited.bmp
.\test.ps1 -SkipBuild  # 预览器已打开时，使用现有 DLL 验证，避免覆盖运行中的 exe
```

后续模拟器方向见 [ROADMAP.md](ROADMAP.md)。
