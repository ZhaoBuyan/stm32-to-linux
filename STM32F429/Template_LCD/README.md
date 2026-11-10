# Template_LCD —— 7 寸彩屏工程模板

> **以后每个新项目都从它复制开始。**
> LTDC / SDRAM / 7 寸屏时序这些坑已经踩完并固化了，不用再配一遍。

---

## 一、这里面有什么

### 已调通并冻结的底座

| 模块 | 位置 | 说明 |
|---|---|---|
| **SDRAM** | `User/sdram/` | FMC bank2 @ `0xD0000000`，16 位宽，CAS 2 |
| **LCD / LTDC** | `User/lcd/` | 800×480 RGB565，PLLSAI `420/7/4` = 15 MHz（约 **32.8 Hz**）|
| **屏型选择** | `bsp_lcd.c` 的 `cur_lcd` | **已设为 `INCH_7`** —— 野火原版默认 `INCH_5`，7 寸屏不改就是黑屏 |
| **时基** | `User/tick/` | TIM6 1 ms（`Tick_Init()` / `Tick_Get()`）|
| **串口** | `User/usart/` | USART1 @115200，`printf` 直接可用 |
| **按键** | `main.c` | KEY1 = PA0、KEY2 = PC13，按下为高 |
| **LED** | `User/led/` | PH10/11/12，共阳极，**低电平亮** |
| **图形库** | `User/gfx/` | ★ 见下 |
| 触摸（可选） | `User/touch/` | GT911，地址 `0xBA`（`bsp_i2c_touch.h` 里 **不要** 定义 `LCD_TOUCH_IC_GT1151QM`）|
| Flash（可选） | `User/flash/` | W25Q 驱动（注意：野火这份是 24 位地址，W25Q256 超 16 MiB 需 4 字节地址）|
| MPU6050（可选） | `User/imu/` | 软件 I2C，PB6/PB7 |

### 图形库 `gfx`（`User/gfx/gfx.c`，约 200 行）

**不用 DMA2D，直接写显存。** 原因是 DMA2D 每次调用都要 `DeInit` + `Init` + 等 TC，
一帧画几十次会和 LTDC 抢 SDRAM，实测就是"一动就花屏"。

| 函数 | 作用 |
|---|---|
| `Gfx_Clear(color)` | 整屏 |
| `Gfx_Fill(x,y,w,h,color)` | 矩形（自动裁剪）|
| `Gfx_FillRound / FillRoundBorder` | 圆角矩形 / 带描边 |
| `Gfx_FillCircle(cx,cy,r,color)` | 实心圆（自己算的，不用野火那个会把垂直线画成斜线的版本）|
| `Gfx_Line(x0,y0,x1,y1,color)` | 直线，2px 粗 |
| `Gfx_Text(x,y,s,fg,bg)` | 字符串 —— **内部处理了 `LCD_DisplayChar` 参数顺序是「纵,横」这个坑** |

颜色统一用 **24 位 `0xRRGGBB`**，内部转 RGB565。

### 演示画面 `demo_scene.c`

把每个原语都用了一遍，改代码时可以直接照着抄。**它不依赖任何业务**，
所以板子和电脑上跑的是同一份代码。

---

## 二、怎么用它

### 用 Keil 烧到板子

```
打开 Project/RVMDK（uv5）/BH-F429.uvprojx
F7 编译 → F8 下载
```

上电会看到：
1. 屏幕自检：红 → 绿 → 蓝 → 白 → 黑（各 0.4 秒）
2. 演示画面：标题、6 色条、圆角面板、折线、运动的小球
3. 按 KEY1 → LED 变黄，画面底部 `KEY1=1`
4. 按 KEY2 → LED 变红

> ⚠️ 上电后**不用做任何校准**（这个模板不读传感器）。

### 用 EmbeddedScreenPreview 在电脑上看（不用烧录）

```
打开 EmbeddedScreenPreview.exe
  → 选本文件夹
  → 自动编译适配器并显示画面
```

命令行也行：

```powershell
EmbeddedScreenPreview.exe -dir <本文件夹路径>
```

第一次会现场用 gcc 编一个适配器（几秒），之后走缓存。

### 开一个新项目

```powershell
# 1. 复制整个文件夹
Copy-Item Template_LCD MyNewProject -Recurse

# 2. 改 Keil 工程名（Project/RVMDK（uv5）/ 里的 .uvprojx 和 .uvoptx）

# 3. 改 main.c 里的业务
#    把 Demo_Draw(...) 换成你的 xxx_Draw(...)

# 4. 往 User/ 下加自己的驱动
```

---

## 三、改的时候最容易踩的几个坑

| 坑 | 现象 | 说明 |
|---|---|---|
| **`cur_lcd` 是 `INCH_5`** | 黑屏 | 7 寸屏必须 `INCH_7`。本模板已改好 |
| **`LCD_DisplayChar` 参数顺序** | 文字竖排 / 挤成一坨 | 是 `(纵, 横)` 且单位是**像素**。用 `Gfx_Text()` 就不用管 |
| **LTDC 时钟配太高** | 花屏、横向条纹 | 15 MHz（32.8 Hz）是这块屏稳定值。21 MHz 会花 |
| **用 RGB888** | 带宽不够 | SDRAM 是 16 位宽，RGB888 每像素 3 字节对不齐总线，必须用 **RGB565** |
| **触摸地址** | `Unknown IC Version` | 7 寸屏是 **GT911**（0xBA），不要定义 `LCD_TOUCH_IC_GT1151QM`（那个宏是给 4.3/5 寸的）|
| **`SysTick_Handler` 里跑触摸** | 串口被刷爆 | 野火 `stm32f4xx_it.c` 每 10 ms 调一次 `GTP_TouchProcess()`，触摸不通时会疯狂重试。本模板里已经注释掉了 |

---

## 四、目录

```
Template_LCD/
├── Project/RVMDK（uv5）/      Keil 工程（已配好，直接编译）
├── Libraries/                 ST 标准外设库
├── User/
│   ├── main.c                 初始化 + 主循环（改这里换业务）
│   ├── gfx/                   ★ 图形库
│   │   ├── gfx.c/.h
│   │   ├── pc_bsp_lcd.c/.h    PC 预览时的假显存桥接
│   │   └── preview_adapter.c  预览器适配器
│   ├── demo/
│   │   └── demo_scene.c/.h    演示画面
│   ├── tick/                  TIM6 1 ms
│   ├── sdram/  lcd/  font/    野火原版
│   ├── usart/  led/  systick/ 野火原版
│   ├── touch/                 野火原版（GT911）
│   ├── flash/                 野火原版（W25Q，24 位地址）
│   └── imu/                   我们写的 MPU6050（软件 I2C）
├── preview.ini                预览器配置
└── 必读说明.txt / 编码说明.md  野火原始说明
```
