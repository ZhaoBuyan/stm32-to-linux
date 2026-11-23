/**
 ******************************************************************************
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS 内核配置（STM32F429IGT6 / Cortex-M4F / Keil RVDS）
 *
 * 板上资源：192 KB SRAM（+ 64 KB CCM），SDRAM 32 MB（已用作显存）
 * 时钟：SYSCLK = 180 MHz（PLL: 25MHz/25*360/2）
 *
 * 几个**必须理解**的配置项，选错了会随机 HardFault 或任务不切换：
 *
 * 1) configPRIO_BITS = 4
 *    STM32 只用优先级寄存器的**高 4 位**（16 级）。
 *    写 8 位会让内核把优先级算错。
 *
 * 2) configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
 *    优先级**数值 >= 5** 的中断才能调用 FreeRTOS 的 FromISR() API。
 *    数值 < 5（= 更紧急）的中断里**绝对不能**碰任何 FreeRTOS 对象。
 *    这是 FreeRTOS 里最容易出隐蔽 bug 的地方。
 *
 * 3) vPortSVCHandler / xPortPendSVHandler / xPortSysTickHandler 重定向
 *    FreeRTOS 的移植层要接管这三个异常。把它们直接映射到 startup 文件里
 *    的向量名，就不用再在 stm32f4xx_it.c 里写转发函数 ——
 *    而且**必须**把 it.c 里原来的同名函数删掉，否则重复定义。
 *
 * 4) configMAX_SYSCALL_INTERRUPT_PRIORITY 的移位
 *    (5 << (8 - 4)) = 0x50。内核写 BASEPRI 时用的是"左对齐"的 8 位值，
 *    不做这个移位，中断屏蔽会失效。
 ******************************************************************************
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ============================================================================
 * 调度器基础
 * ========================================================================== */
#define configUSE_PREEMPTION                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1   /* CM4 有 CLZ 指令 */
#define configUSE_TICKLESS_IDLE                   0   /* 本项目不用低功耗 */
#define configCPU_CLOCK_HZ                        ( 180000000UL )
#define configTICK_RATE_HZ                        ( 1000 )   /* 1 ms 一个 tick */
#define configMAX_PRIORITIES                      ( 8 )
#define configMINIMAL_STACK_SIZE                  ( 128 )    /* 单位：字（Word）*/
#define configMAX_TASK_NAME_LEN                   ( 16 )
#define configUSE_16_BIT_TICKS                    0
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_TIME_SLICING                    1
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configUSE_NEWLIB_REENTRANT                0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0
#define configSTACK_DEPTH_TYPE                    uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE          size_t

/* ============================================================================
 * 内存
 * ========================================================================== */
#define configSUPPORT_STATIC_ALLOCATION           0
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configAPPLICATION_ALLOCATED_HEAP          0
#define configTOTAL_HEAP_SIZE                     ( 40 * 1024 )   /* 40 KB */

/* ============================================================================
 * 同步原语（本项目演示队列 / 互斥量 / 软件定时器）
 * ========================================================================== */
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configQUEUE_REGISTRY_SIZE                 8             /* 调试器里能看队列名 */
#define configUSE_QUEUE_SETS                      0
#define configUSE_TASK_NOTIFICATIONS              1

/* ============================================================================
 * 软件定时器
 * ========================================================================== */
#define configUSE_TIMERS                          1
#define configTIMER_TASK_PRIORITY                 ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                  10
#define configTIMER_TASK_STACK_DEPTH              ( 256 )

/* ============================================================================
 * 协程（不用）
 * ========================================================================== */
#define configUSE_CO_ROUTINES                     0
#define configMAX_CO_ROUTINE_PRIORITIES           2

/* ============================================================================
 * 钩子函数
 * ========================================================================== */
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configUSE_MALLOC_FAILED_HOOK              1   /* 堆耗尽时进钩子，比 HardFault 好查 */
#define configUSE_DAEMON_TASK_STARTUP_HOOK        0

/* ============================================================================
 * 调试
 * ========================================================================== */
#define configCHECK_FOR_STACK_OVERFLOW            2   /* 方法 2：检查栈尾魔数 + 越界 */

#define configGENERATE_RUN_TIME_STATS             0
#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      1   /* 打印任务列表 */
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 0

/* ============================================================================
 * 需要编进内核的 API
 * ========================================================================== */
#define INCLUDE_vTaskPrioritySet               1
#define INCLUDE_uxTaskPriorityGet              1
#define INCLUDE_vTaskDelete                    1
#define INCLUDE_vTaskSuspend                   1
#define INCLUDE_vTaskDelayUntil                1
#define INCLUDE_vTaskDelay                     1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xQueueGetMutexHolder            1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTaskGetHandle                  1

/* ============================================================================
 * Cortex-M4F 中断优先级
 * ========================================================================== */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                        __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                        4
#endif

/* 能设的最低优先级（数值越小优先级越高）*/
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY            15

/* 允许调用 FromISR() API 的中断，优先级数值最大到 5；
 * 数值 < 5 的中断（更紧急）里禁止调用任何 FreeRTOS API。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY        5

/* 内核自己用这两个值写寄存器，需要左移到 8 位的高位 */
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ============================================================================
 * 断言与中断向量重定向
 * ========================================================================== */

/* configASSERT：出问题时停机（配合调试器看是哪一条断言）
 * 不用 while(1) 而是 disable interrupt + 死循环，避免被其它中断干扰。 */
extern void vAssertFailed( const char *file, int line );
#define configASSERT( x ) \
    do { if( ( x ) == 0 ) { vAssertFailed( __FILE__, __LINE__ ); } } while( 0 )

/* 把 FreeRTOS 移植层的三个异常处理直接映射到 startup 里的向量名。
 * 这样 stm32f4xx_it.c 里就**不能再**定义 SVC_Handler / PendSV_Handler /
 * SysTick_Handler，否则链接时报重复定义。 */
#define vPortSVCHandler          SVC_Handler
#define xPortPendSVHandler       PendSV_Handler
#define xPortSysTickHandler      SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
