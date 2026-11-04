# 姿态感知交互游戏（MPU6050 倾斜控制小球）

> STM32F429 挑战者 V2 + 7 寸 800×480 RGB 电容屏
> 用板载 MPU6050 感知倾斜，控制屏幕上的小球滚到目标区；避开红区、绕过墙体。
> 6 关，可玩通、可失败、可重试、可切关。

![demo](Evidence/演示视频.mp4)

---

## 一、硬件

| 部件 | 型号 / 接口 | 说明 |
|---|---|---|
| 主控 | STM32F429IGT6 | 180 MHz（HSE 25MHz → PLL 15/216/2）|
| 加速度计 | **板载 MPU6050** | **I2C1 / PB6 / PB7**，7 位地址 `0x68`（8 位 `0xD0`）|
| 显示屏 | **7 寸 RGB 电容屏** | 800×480，**RGB565**，**32.8 Hz** |
| 触摸 | **GT911** | I2C2 / PH4 / PH5，地址 `0xBA`（7 位 `0x5D`）|
| 按键 | KEY1 = **PA0**，KEY2 = **PC13** | 板上已有下拉，按下为高 |
| RGB LED | 红 **PH10** / 绿 **PH11** / 蓝 **PH12** | **共阳极**，低电平点亮 |
| 串口 | USART1 / PA9 / PA10 | 115200-8-N-1，调试日志 |

> ⚠️ **7 寸屏必须用 GT911 的地址**：`bsp_i2c_touch.h` 里的
> `#define LCD_TOUCH_IC_GT1151QM` 是给 4.3/5 寸屏用的，**必须注释掉**。
> 详见 `DEBUG.md` 第 11 条。

---

## 二、软件结构

分四层，**只有最底下一层碰硬件**：

```
   main.c          初始化、主循环、按键/触摸分发
     │
     ├── app.c     状态机（READY/RUNNING/PAUSED/WIN/FAIL）+ 按钮布局 + 点击命中
     │     │
     │     └── game.c   物理（加速度/摩擦/限速）+ 碰撞 + 6 关数据
     │           │
     │           └── render.c   画到显存（唯一碰 LCD 的模块）
     │
     └── mpu6050_bb.c   MPU6050 驱动（自带软件 I2C，跑在 PB6/PB7）
```

| 文件 | 职责 | 行数 | 特点 |
|---|---|---|---|
| `User/game/game.c` | 物理、碰撞、关卡 | ~330 | **纯 C，不依赖任何库**，可在 PC 上测 |
| `User/game/app.c` | 状态机、按钮 | ~220 | **纯 C**，可在 PC 上测 |
| `User/game/render.c` | 绘制、局部刷新 | ~360 | 只碰「写显存」和「画字符」 |
| `User/game/bsp_tick.c` | TIM6 1ms 节拍 | ~50 | 中断里只做 `++` |
| `User/imu/mpu6050_bb.c` | MPU6050 驱动 | ~280 | 自带软件 I2C（开漏 + 上拉）|
| `User/main.c` | 初始化 + 主循环 | ~250 | — |

**这个分层带来的好处**：

- `game.c` / `app.c` 是纯 C → **在 PC 上跑过 53 条测试**（`tests_game_pc.c`）
- 换屏幕 / 换硬件只改 `render.c` 和 `main.c`
- 想做 PC 预览器 / 模拟器，只需要替换「写显存」那一层

---

## 三、玩法

| 状态 | 触发 | 表现 |
|---|---|---|
| **READY** | 上电 | 显示关卡、`START` 按钮，白灯 |
| **RUNNING** | 点 START / 按 KEY1 | 小球随倾斜滚动，蓝灯 |
| **PAUSED** | 点 PAUSE / 按 KEY1 | 球定住，`RESUME` / `RESTART`，黄灯 |
| **WIN** | 球进绿色目标区 | `NEXT` / `REPLAY`，**绿灯** |
| **FAIL** | 球碰红色危险区 | `RETRY`，**红灯** |

### 十二关（两段式递进）

**第一段 1~6：迷宫** —— 只有墙，先把"倾斜控制"练熟

| 关 | 障碍 | 目标 | 设计意图 |
|---|---|---|---|
| 1 | 无 | 右上 | 空场，斜穿全屏摸手感 |
| 2 | 横墙从左伸出 | 右上 | 只能走右边 |
| 3 | 横墙从右伸出 | 右上 | 对称练习，换一边想 |
| 4 | 竖墙封上半 | 右下 | 必须从下面绕 |
| 5 | 两道错开竖墙 | 右上 | **S 形路线** |
| 6 | 长横墙 + 右上短竖墙 | 右下 | 绕两段 |

**第二段 7~12：加红区** —— 在迷宫基础上再压一个危险区

| 关 | 障碍 | 目标 | 新增压力 |
|---|---|---|---|
| 7 | 横墙 | 右上 | 红区堵右侧上升通道 |
| 8 | 横墙 | 右上 | 红区守左上 |
| 9 | 竖墙 | 右下 | 红区卡中间，只能贴左边下 |
| 10 | 两道竖墙 | 右上 | 红区封底部，得从缝里钻 |
| 11 | 三段墙 | 右下 | 路线要折返 |
| 12 | 三段墙 | **左上** | 目标回到起点附近，全程折返 |

**12 关全部通过"网格 BFS 洪水填充"验证存在通路**（见 `tests_game_pc.c`）。

---

## 四、操作

| 操作 | 方式 |
|---|---|
| 移动小球 | **倾斜开发板**（前后左右）|
| 开始 / 暂停 / 继续 / 下一关 / 重试 | **点屏幕上的按钮** 或 **按 KEY1** |
| 重玩本关 | 点 `RESTART` / `REPLAY` 或 **按 KEY2** |

> 触摸和按键**并存**：哪个方便用哪个。

---

## 五、关键设计（答辩要点）

### 1. 静止校准 + 水平参考

板子放平实测 `ax ≈ 122`（不是 0）。如果直接拿它当倾斜量，球会一直往一边漂。

```c
/* 上电时采 100 次平均，记下"放平时读多少" */
CalibrateLevel(100);
/* 之后都用差值判断倾斜 */
tx = d.ax - g_ax_ref;
```

### 2. 方向标定（实测，不是猜的）

| 动作 | `tilt_x` | 球应该往哪 | 结论 |
|---|---|---|---|
| 抬高板子**左边** | `-6300`（负）| **右** | 所以 `vx = -tilt_x × k` |

```c
#define GAME_SIGN_X  (-1.0f)
#define GAME_SIGN_Y  (+1.0f)     /* 抬远端 → 球往自己这边滚 */
```

### 3. 死区

```c
#define GAME_DEADZONE_LSB  150.0f    /* 约 0.5°，滤掉手的微小抖动 */
```
实测静止噪声 ±50 LSB，死区取 150 足够。

### 4. 碰撞用「球的整体」，不是球心

任务书明确要求「不得只依靠小球圆心作边界和矩形碰撞判断」。

```c
/* 球按边长 2R 的方块参与碰撞 */
bl = b->x - GAME_BALL_R;   br = b->x + GAME_BALL_R;
...
/* 选【最小重叠方向】推出，避免从角上穿过去 */
```

### 5. 局部刷新（省带宽）

```
Render_Begin   关卡/状态变化时整屏重画一次
Render_Frame   每帧只做三步：
               ① 用背景色擦掉旧球（一个 21 行的圆）
               ② 补画被旧球压到的墙/红区/目标/按钮
               ③ 画新球
```

**稳定运行时每帧只写约 400 像素（≈800 字节），而不是 384000 像素（768KB）。**

### 6. 直接写显存，不用 DMA2D

`LCD_DrawFullRect` / `LCD_DrawFullCircle` 内部每次都 `DMA2D_DeInit()` + `Init()` + 等 TC，
一帧里球要擦旧+画新好几次，会和 LTDC 抢 SDRAM。

```c
static void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color)
{
    uint16_t c565 = to565(color);
    volatile uint8_t *p = FB_BASE + 2u * ((y) * LCD_PIXEL_WIDTH + (x));
    ...
}
```

### 7. 显示参数怎么定下来的

| 方案 | 带宽 | 刷新率 | 结果 |
|---|---|---|---|
| RGB888 + 8.75MHz | 22 MB/s | 19 Hz | 干净但**闪** |
| RGB888 + 21MHz | 53 MB/s | 46 Hz | 不闪但**花屏** ❌ |
| **RGB565 + 15MHz** | **25 MB/s** | **32.8 Hz** | ✅ **既清楚又不花** |

**关键**：SDRAM 是 **16 位宽**，RGB888 每像素 3 字节对不齐总线，
每个像素要"读-改-写"；RGB565 的 2 字节正好一次传输。

### 8. 中断只做一件事

```c
void TIM6_DAC_IRQHandler(void)
{
    s_ms++;          /* 就这一行 */
}
```

采 IMU、算物理、刷屏全在主循环里做。

---

## 六、构建与烧录

1. 用 Keil 打开 `Project/RVMDK（uv5）/BH-F429.uvprojx`
2. `F7` 编译（0 Error）
3. `F8` 下载
4. 串口 115200 打开，会看到：

```
=== F429 tilt ball game ===
LCD ready (cur_lcd=1)
touch init ret=0 -> OK
IMU WHO_AM_I = 0x68  OK
calibrating: keep the board FLAT and STILL ...
gyro bias = (205, -171, 35)
level ref = ax:122 ay:-40
ready.
init game ...
render first frame ...
game ready: state=READY level=1
```

> ⚠️ **上电后立刻把板子放平别动** —— 会做约 1 秒的静止校准。

### PC 端验证（不需要板子）

```powershell
cd C:\game_build
gcc -std=c11 -Wall -Wextra -O2 -o game_test.exe game.c tests_game_pc.c
.\game_test.exe        # 53 条断言，覆盖物理/碰撞/关卡可达性
```

---

## 七、目录

```
Game_LCD/
├── Project/RVMDK（uv5）/BH-F429.uvprojx   Keil 工程
├── User/
│   ├── main.c                 初始化 + 主循环
│   ├── game/                  ★ 我们自己写的
│   │   ├── game.c/h           物理、碰撞、6 关
│   │   ├── app.c/h            状态机、按钮
│   │   ├── render.c/h         绘制、局部刷新
│   │   └── bsp_tick.c/h       TIM6 1ms
│   ├── imu/
│   │   └── mpu6050_bb.c/h     MPU6050（软件 I2C）
│   ├── lcd/ sdram/ touch/ ... 野火原版驱动
│   └── led/ usart/ systick/
├── tests_game_pc.c            PC 端测试（53 条断言）
├── Evidence/演示视频.mp4       演示视频
├── DEBUG.md                   ★ 11 条真实故障排查记录
├── 编码说明.md                 野火源码是 GBK，怎么在编辑器里正常看
└── 必读说明.txt               野火原始说明
```

---

## 八、已知限制

| 限制 | 原因 | 影响 |
|---|---|---|
| 球是**方块**还是**圆**？ | 现在是**自绘圆**（逐行算 `sqrt(r²-dy²)`）| ✅ 已经是圆 |
| 32.8 Hz 刷新 | 受 SDRAM 带宽限制 | 快速移动时可能有轻微撕裂 |
| 文字用野火的 16×24 点阵 | 未自带字体 | 只有 ASCII |
| `LCD_DrawLine` 的 bug 没修 | 换了规避方案（自己画）| 以后别用野火的画线/画圆 |

---

## 九、踩过的坑

**11 条真实故障的完整排查过程 → [DEBUG.md](DEBUG.md)**

其中 5 条是**野火库自身的 bug 或默认值不适合本板**：

- `SysTick_Handler` 里跑触摸轮询，外设不通时刷爆串口
- `cur_lcd` 默认 5 寸，7 寸屏黑屏
- `LCD_DrawLine` 垂直偏移把像素当字节，一动就花屏
- `LCD_TOUCH_IC_GT1151QM` 宏导致触摸找错地址
- `LCD_DisplayChar` 参数名与实际语义不符
---

## 十、PC 预览（不用烧录就能看）

本工程带了一个预览器适配器，配合独立仓库
**[EmbeddedScreenPreview](https://github.com/ZhaoBuyan/EmbeddedScreenPreview)** 使用：

| 文件 | 作用 |
|---|---|
| `User/game/preview_adapter.c` | 适配器：把 `Pv_*` 转发给 `app.c` / `game.c` / `render.c` |
| `User/game/pc_bsp_lcd.c/.h` | 把野火的 LCD 接口映射到预览器的帧缓冲 |
| `preview.ini` | 告诉预览器：分辨率 800×480、30fps、要编译哪些源文件 |

### 用法

```powershell
# 1. 先把预览器编出来（只需一次）
#    仓库：https://github.com/ZhaoBuyan/EmbeddedScreenPreview
cd <预览器目录>
.\build.ps1

# 2. 打开本工程
.\fbview.exe -dir <本仓库>\STM32F429\Game_LCD
```

预览器会读 `preview.ini` 里的 `sources=`，**自动用 gcc 把适配器编成 DLL**；
之后再打开就直接加载，不用重复编译。

也可以先手动编好：

```powershell
gcc -std=c11 -O2 -shared -DFBVIEW_BUILD_DLL -DPC_PREVIEW `
    -I<预览器目录> -IUser\game `
    -o game_adapter.dll `
    User\game\preview_adapter.c User\game\pc_bsp_lcd.c `
    User\game\game.c User\game\app.c User\game\render.c
```

### 能验证什么

| ✅ | ❌ |
|---|---|
| 配色、圆角、横幅排版 | LTDC 时序、刷新率、撕裂 |
| 手感（加速度、摩擦、死区）| SDRAM 带宽（预览器有粗略模型，不是真时序）|
| 关卡设计能不能过 | 触摸 IC / 中断 / DMA |
| 状态机与按钮流程 | 真实传感器方向 |

> ⚠️ **固件不受影响**：`render.c` 里只有一个 `#ifdef PC_PREVIEW` 的 `#include` 分支，
> 不定义这个宏时走的还是野火驱动。已确认 Keil 编译 0 Error 0 Warning。
