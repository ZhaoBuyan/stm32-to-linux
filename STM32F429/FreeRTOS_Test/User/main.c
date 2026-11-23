/**
 ******************************************************************************
 * @file    main.c
 * @brief   FreeRTOS 集成演示（STM32F429 + StdPeriph 库）
 *
 * 演示内容（课程要求"集成"，这里把最常用的五个东西都用上）：
 *
 *   ① 多任务 + 优先级     4 个任务，不同优先级、不同周期
 *   ② 队列                按键任务 -> UI 任务，传事件
 *   ③ 互斥量              保护 printf（串口不是线程安全的）
 *                         和保护 LCD（两个任务都会画屏）
 *   ④ 软件定时器          1 秒翻转 LED，不占任务栈
 *   ⑤ 运行时统计          空闲堆、各任务栈高水位
 *
 * 任务表：
 *   ┌────────────┬──────┬────────┬───────────────────────────┐
 *   │ 任务        │ 优先级│ 周期   │ 干什么                     │
 *   ├────────────┼──────┼────────┼───────────────────────────┤
 *   │ TaskKey    │  4   │ 20 ms  │ 扫按键、消抖、发队列         │
 *   │ TaskUi     │  3   │ 事件/200ms │ 收队列、更新屏幕        │
 *   │ TaskBeat   │  2   │ 500 ms │ 心跳计数（证明在跑）        │
 *   │ TaskStat   │  1   │ 2000 ms│ 串口打印统计                │
 *   │ TmrLed     │  -   │ 1000 ms│ 软件定时器翻转 LED          │
 *   └────────────┴──────┴────────┴───────────────────────────┘
 *
 * ⚠️ 串口输出全部 ASCII：ARMCC 按 GBK 读源文件，UTF-8 中文会吞掉引号。
 ******************************************************************************
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "stm32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include "./led/bsp_led.h"
#include "./usart/bsp_debug_usart.h"
#include "./lcd/bsp_lcd.h"
#include "./gfx/gfx.h"
#include "./rtos/bsp_delay.h"

/* ============================================================================
 * 屏幕配色
 * ========================================================================== */
#define COL_BG      0x10141C
#define COL_PANEL   0x1A2230
#define COL_BORDER  0x2A3442
#define COL_TITLE   0xE6EDF3
#define COL_DIM     0x8B98A9
#define COL_OK      0x35D07F
#define COL_WARN    0xFFB020
#define COL_ACCENT  0x4A90D9

/* ============================================================================
 * 全局对象
 * ========================================================================== */
typedef enum
{
    KEY_EVT_KEY1 = 1,
    KEY_EVT_KEY2 = 2
} KeyEvent;

static QueueHandle_t     g_q_key   = NULL;   /* 按键任务 -> UI 任务 */
static SemaphoreHandle_t g_mtx_uart = NULL;  /* 保护 printf */
static SemaphoreHandle_t g_mtx_lcd  = NULL;  /* 保护 LCD 绘制 */
static TimerHandle_t     g_tmr_led  = NULL;  /* 软件定时器 */

/* 各任务的心跳计数，用来在屏幕上证明"它真的在跑" */
static volatile uint32_t g_cnt_key  = 0;
static volatile uint32_t g_cnt_ui   = 0;
static volatile uint32_t g_cnt_beat = 0;
static volatile uint32_t g_cnt_stat = 0;
static volatile uint32_t g_tmr_ticks = 0;

/* UI 的最新状态 */
static volatile uint32_t g_last_key  = 0;
static volatile uint32_t g_key_total = 0;

/* 任务句柄（打印栈高水位要用） */
static TaskHandle_t g_h_key  = NULL;
static TaskHandle_t g_h_ui   = NULL;
static TaskHandle_t g_h_beat = NULL;
static TaskHandle_t g_h_stat = NULL;

/* ============================================================================
 * 带互斥量的串口打印
 *
 * printf 最终走 USART1 的阻塞发送，**不是线程安全的**：
 * 两个任务同时打印会互相插入，输出变成乱码。
 * 这就是互斥量最典型的用途。
 * ========================================================================== */
static void print_locked(const char *fmt, ...)
{
    va_list ap;

    if (xSemaphoreTake(g_mtx_uart, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;             /* 100ms 拿不到就不打了，绝不无限等 */
    }

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    xSemaphoreGive(g_mtx_uart);
}

/* ============================================================================
 * 任务 1：按键扫描（最高优先级）
 *
 * 每 20 ms 扫一次，做 3 次一致性确认（消抖）。
 * 检测到"按下沿"就把事件塞进队列 —— 这里**不直接画屏**，
 * 因为按键任务不该被画屏耽误（画屏要写 750KB 显存）。
 * ========================================================================== */
static void TaskKey(void *arg)
{
    uint8_t  last1 = 0, last2 = 0;
    uint8_t  cnt1 = 0, cnt2 = 0;

    (void)arg;

    for (;;)
    {
        uint8_t k1 = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0)  == Bit_SET) ? 1u : 0u;
        uint8_t k2 = (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13) == Bit_SET) ? 1u : 0u;

        g_cnt_key++;

        /* 连续 3 次读到相同值才认为稳定（20ms x 3 = 60ms 消抖）*/
        if (k1 == last1) { if (cnt1 < 3u) { cnt1++; } }
        else             { cnt1 = 0; last1 = k1; }

        if (k2 == last2) { if (cnt2 < 3u) { cnt2++; } }
        else             { cnt2 = 0; last2 = k2; }

        /* 上升沿 = 按下（板上按键按下为高）*/
        if (cnt1 == 3u)
        {
            KeyEvent ev = KEY_EVT_KEY1;
            cnt1 = 4u;                      /* 防止重复触发 */
            (void)xQueueSend(g_q_key, &ev, 0);
            g_key_total++;
            g_last_key = 1;
        }
        if (cnt2 == 3u)
        {
            KeyEvent ev = KEY_EVT_KEY2;
            cnt2 = 4u;
            (void)xQueueSend(g_q_key, &ev, 0);
            g_key_total++;
            g_last_key = 2;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ============================================================================
 * 任务 2：UI 更新
 *
 * 用**带超时的队列接收**：有事件就立刻处理，没事件每 200 ms 刷一次屏。
 * 这样既响应快，又不会空转烧 CPU。
 * ========================================================================== */
static void draw_screen(void)
{
    char buf[80];
    uint32_t tick = (uint32_t)xTaskGetTickCount();

    Gfx_Clear(COL_BG);

    Gfx_Text(16, 12, "FreeRTOS integration demo", COL_TITLE, COL_BG);
    sprintf(buf, "tick %lu  up %lu.%03lu s",
            (unsigned long)tick,
            (unsigned long)(tick / 1000u), (unsigned long)(tick % 1000u));
    Gfx_Text(16, 40, buf, COL_DIM, COL_BG);

    /* 四个任务的心跳：数字一直在涨 = 任务都在跑 */
    Gfx_FillRoundBorder(16, 78, 768, 112, 10, COL_PANEL, COL_BORDER);
    Gfx_Text(32, 92, "task heartbeat (keeps increasing = alive)", COL_DIM, COL_PANEL);

    sprintf(buf, "TaskKey   prio 4   20ms    %lu", (unsigned long)g_cnt_key);
    Gfx_Text(32, 118, buf, COL_TITLE, COL_PANEL);
    sprintf(buf, "TaskUi    prio 3   evt/200 %lu", (unsigned long)g_cnt_ui);
    Gfx_Text(32, 142, buf, COL_TITLE, COL_PANEL);
    sprintf(buf, "TaskBeat  prio 2   500ms   %lu", (unsigned long)g_cnt_beat);
    Gfx_Text(420, 118, buf, COL_TITLE, COL_PANEL);
    sprintf(buf, "TaskStat  prio 1   2000ms  %lu", (unsigned long)g_cnt_stat);
    Gfx_Text(420, 142, buf, COL_TITLE, COL_PANEL);
    sprintf(buf, "timer 1000ms  %lu", (unsigned long)g_tmr_ticks);
    Gfx_Text(32, 166, buf, COL_ACCENT, COL_PANEL);

    /* 队列 + 事件 */
    Gfx_FillRoundBorder(16, 206, 768, 80, 10, COL_PANEL, COL_BORDER);
    Gfx_Text(32, 220, "queue (TaskKey -> TaskUi)", COL_DIM, COL_PANEL);
    sprintf(buf, "waiting %u   total events %lu   last %s",
            (unsigned)uxQueueMessagesWaiting(g_q_key),
            (unsigned long)g_key_total,
            (g_last_key == 1u) ? "KEY1" : (g_last_key == 2u) ? "KEY2" : "-");
    Gfx_Text(32, 248, buf, (g_key_total > 0u) ? COL_OK : COL_DIM, COL_PANEL);

    /* 内存 */
    Gfx_FillRoundBorder(16, 302, 768, 80, 10, COL_PANEL, COL_BORDER);
    Gfx_Text(32, 316, "heap_4", COL_DIM, COL_PANEL);
    sprintf(buf, "free %lu B / total %lu B   min-free %lu B",
            (unsigned long)xPortGetFreeHeapSize(),
            (unsigned long)(configTOTAL_HEAP_SIZE),
            (unsigned long)xPortGetMinimumEverFreeHeapSize());
    Gfx_Text(32, 344, buf, COL_TITLE, COL_PANEL);

    Gfx_Text(16, 400, "KEY1/KEY2 = press to send a queue event", COL_DIM, COL_BG);
    Gfx_Text(16, 428, "stats printed on USART1 every 2 s", COL_DIM, COL_BG);
}

static void TaskUi(void *arg)
{
    (void)arg;

    for (;;)
    {
        KeyEvent ev;

        /* 等事件；最多等 200ms */
        if (xQueueReceive(g_q_key, &ev, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            /* 收到按键事件：在串口打一行，并让 LED 闪一下 */
            print_locked("[ui] key event: %s\r\n",
                         (ev == KEY_EVT_KEY1) ? "KEY1" : "KEY2");

            if (ev == KEY_EVT_KEY1) { LED_GREEN;  }
            else                    { LED_RED;    }
        }

        g_cnt_ui++;

        /* 刷屏 —— 必须拿 LCD 互斥量 */
        if (xSemaphoreTake(g_mtx_lcd, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            draw_screen();
            xSemaphoreGive(g_mtx_lcd);
        }
    }
}

/* ============================================================================
 * 任务 3：心跳（只做延迟，证明调度器在按周期切任务）
 * ========================================================================== */
static void TaskBeat(void *arg)
{
    (void)arg;

    for (;;)
    {
        g_cnt_beat++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ============================================================================
 * 任务 4：串口统计（优先级最低，保证不影响其他任务）
 * ========================================================================== */
static void TaskStat(void *arg)
{
    (void)arg;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));

        g_cnt_stat++;

        print_locked("\r\n===== RTOS stats @ %lu ms =====\r\n",
                     (unsigned long)xTaskGetTickCount());
        print_locked("heap   free %lu / %lu B, min-free %lu B\r\n",
                     (unsigned long)xPortGetFreeHeapSize(),
                     (unsigned long)configTOTAL_HEAP_SIZE,
                     (unsigned long)xPortGetMinimumEverFreeHeapSize());
        print_locked("queue  waiting %u, total events %lu\r\n",
                     (unsigned)uxQueueMessagesWaiting(g_q_key),
                     (unsigned long)g_key_total);
        print_locked("beat   key %lu  ui %lu  beat %lu  stat %lu  timer %lu\r\n",
                     (unsigned long)g_cnt_key, (unsigned long)g_cnt_ui,
                     (unsigned long)g_cnt_beat, (unsigned long)g_cnt_stat,
                     (unsigned long)g_tmr_ticks);

        /* 栈高水位：单位是"字"，剩得越少越危险 */
        print_locked("stack  (words left, higher = safer)\r\n");
        print_locked("       TaskKey  %u\r\n",
                     (unsigned)uxTaskGetStackHighWaterMark(g_h_key));
        print_locked("       TaskUi   %u\r\n",
                     (unsigned)uxTaskGetStackHighWaterMark(g_h_ui));
        print_locked("       TaskBeat %u\r\n",
                     (unsigned)uxTaskGetStackHighWaterMark(g_h_beat));
        print_locked("       TaskStat %u\r\n",
                     (unsigned)uxTaskGetStackHighWaterMark(g_h_stat));
        print_locked("===============================\r\n");
    }
}

/* ============================================================================
 * 软件定时器回调（在定时器服务任务里执行，不在中断里）
 * ========================================================================== */
static void TmrLedCallback(TimerHandle_t xTimer)
{
    (void)xTimer;

    g_tmr_ticks++;

    /* 每 2 个周期（2 秒）翻一次蓝色 LED，避免太晃眼 */
    if ((g_tmr_ticks & 1u) != 0u) { LED_BLUE; }
    else                          { LED_GREEN; }
}

/* ============================================================================
 * 内核钩子
 * ========================================================================== */

/* configASSERT 触发时停在这里，调试器能直接看到出错的文件和行号 */
void vAssertFailed(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();

    /* 打印出来（此时调度器可能还没启动，printf 仍可用）*/
    printf("\r\n[ASSERT FAILED] %s:%d\r\n", file, line);

    for (;;) { }
}

/* 堆耗尽：比 HardFault 好查得多 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    printf("\r\n[MALLOC FAILED] heap exhausted (configTOTAL_HEAP_SIZE too small?)\r\n");
    for (;;) { }
}

/* 栈溢出（configCHECK_FOR_STACK_OVERFLOW = 2）*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    taskDISABLE_INTERRUPTS();
    printf("\r\n[STACK OVERFLOW] task \"%s\"\r\n", pcTaskName);
    for (;;) { }
}

/* ============================================================================
 * 主函数
 * ========================================================================== */
int main(void)
{
    /* ---------------- 1. 基础延时（DWT）----------------
     * 必须在 LCD 初始化之前：LCD 复位时序要用到延时，
     * 而原来的 SysTick 版延时已经被 FreeRTOS 接管了。 */
    Delay_Init();

    /* ---------------- 2. NVIC 优先级分组 ----------------
     * ⚠️ FreeRTOS 要求**全部 4 位都作抢占优先级**，即 Group 4。
     *    不加这一句，STM32 复位后默认是"4 位抢占 + 0 位子优先级"以外的分组
     *    （实际取决于上一次的 PRIGROUP 值），会让
     *    configMAX_SYSCALL_INTERRUPT_PRIORITY 的比较失效 ——
     *    表现为"某个中断里调用 FromISR() 随机 HardFault"，极难查。
     *    必须在初始化任何带中断的外设**之前**调用。 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* ---------------- 3. 外设 ---------------- */
    LED_GPIO_Config();
    Debug_USART_Config();

    printf("\r\n==========================================\r\n");
    printf(" FreeRTOS integration demo (F429, StdPeriph)\r\n");
    printf(" kernel v11.3.1  port: RVDS/ARM_CM4F\r\n");
    printf("==========================================\r\n");

    LCD_Init();
    LCD_LayerInit();
    LTDC_Cmd(ENABLE);
    LCD_SetLayer(LCD_BACKGROUND_LAYER);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetLayer(LCD_FOREGROUND_LAYER);
    LCD_SetTransparency(0xFF);
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_SetFont(&LCD_DEFAULT_FONT);

    LED_BLUE;

    /* ---------------- 4. 创建 RTOS 对象 ----------------
     * 都在调度器启动前创建，所以任何失败都只能在这里死循环报错。 */
    g_q_key = xQueueCreate(8, sizeof(KeyEvent));
    if (g_q_key == NULL) { printf("[FATAL] queue create failed\r\n"); for (;;) { } }
    vQueueAddToRegistry(g_q_key, "KeyQueue");

    g_mtx_uart = xSemaphoreCreateMutex();
    if (g_mtx_uart == NULL) { printf("[FATAL] uart mutex create failed\r\n"); for (;;) { } }
    vQueueAddToRegistry(g_mtx_uart, "UartMutex");

    g_mtx_lcd = xSemaphoreCreateMutex();
    if (g_mtx_lcd == NULL) { printf("[FATAL] lcd mutex create failed\r\n"); for (;;) { } }
    vQueueAddToRegistry(g_mtx_lcd, "LcdMutex");

    g_tmr_led = xTimerCreate("LedTmr",
                             pdMS_TO_TICKS(1000),
                             pdTRUE,              /* 自动重载 */
                             NULL,
                             TmrLedCallback);
    if (g_tmr_led == NULL) { printf("[FATAL] timer create failed\r\n"); for (;;) { } }

    /* ---------------- 5. 创建任务 ----------------
     * 栈的单位是"字"（4 字节）：256 字 = 1 KB */
    if (xTaskCreate(TaskKey,  "TaskKey",  256, NULL, 4, &g_h_key)  != pdPASS) { for (;;) { } }
    if (xTaskCreate(TaskUi,   "TaskUi",   512, NULL, 3, &g_h_ui)   != pdPASS) { for (;;) { } }
    if (xTaskCreate(TaskBeat, "TaskBeat", 256, NULL, 2, &g_h_beat) != pdPASS) { for (;;) { } }
    if (xTaskCreate(TaskStat, "TaskStat", 384, NULL, 1, &g_h_stat) != pdPASS) { for (;;) { } }

    printf("objects and tasks created:\r\n");
    printf("  queue    8 x KeyEvent\r\n");
    printf("  mutex    uart, lcd\r\n");
    printf("  timer    1000 ms auto-reload\r\n");
    printf("  tasks    TaskKey(4) TaskUi(3) TaskBeat(2) TaskStat(1)\r\n");
    printf("  heap     %lu B free before scheduler\r\n",
           (unsigned long)xPortGetFreeHeapSize());
    printf("starting scheduler...\r\n");

    /* ---------------- 6. 启动调度器（不返回）---------------- */
    vTaskStartScheduler();

    /* 只有堆不足导致调度器启动失败才会走到这里 */
    printf("\r\n[FATAL] scheduler failed to start (heap too small?)\r\n");
    for (;;) { }
}

/*********************************************END OF FILE**********************/
