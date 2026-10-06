/**
 * @file    rtconfig.h
 * @brief   RT-Thread 内核配置 (N32G452 / GCC)
 *
 *  本工程的目标是"把 RT-Thread 功能全开跑起来验证移植成功",
 *  所以除少数确实无法启用的项之外, 组件能开就开。
 *
 *  ── 三项刻意关闭, 原因写在下面, 不是漏了 ──
 *    RT_USING_LIBC   本工程已用 src/syscalls.c 提供 newlib 的 _write/_read/_close 等桩函数;
 *                    RT-Thread 的 newlib 端口(libc/compilers/newlib)提供的是一整套
 *                    _write_r/_read_r/_sbrk_r 可重入版本, 两份会语义重叠、互相遮蔽。
 *                    要改用 RT-Thread 的版本, 就得删掉 src/syscalls.c, 属于另一件事。
 *    RT_USING_DFS    需要挂真实文件系统(SPI Flash/ROMFS), 本工程暂无存储介质。
 *    RT_USING_SIGNALS 依赖 libc 的 POSIX 支持, 同上。
 *
 *  参照厂商例程 projects/n32g45x_EVAL/examples/RT_Thread 下各例程的 inc/rtconfig.h
 */
#ifndef __RTTCONFIG_H__
#define __RTTCONFIG_H__

/* ==========================================================================
 * 内核基础
 * ========================================================================== */
#define RT_NAME_MAX                 8
#define RT_ALIGN_SIZE               4
#define RT_THREAD_PRIORITY_MAX      32

/* 1ms system tick. GP21 sampling is paced by external START/STOP, not the tick. */
#define RT_TICK_PER_SECOND          1000

#define IDLE_THREAD_STACK_SIZE      256

/* ==========================================================================
 * 调试与自检  —— 验证移植是否成功主要靠这些
 * ========================================================================== */
#define RT_DEBUG                    /* 打开 RT_ASSERT 等断言 */
#define RT_DEBUG_INIT           1   /* 启动时打印每个 INIT_*_EXPORT 的执行顺序 */
#define RT_USING_OVERFLOW_CHECK     /* 线程栈溢出检测 */

/* 内核钩子 (线程切换/空闲钩子等), 便于做 CPU 占用统计 */
#define RT_USING_HOOK

/* ==========================================================================
 * 内存管理
 * ========================================================================== */
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM
#define RT_USING_TINY_SIZE
#define RT_USING_MEMPOOL            /* 固定块内存池 */

/* ==========================================================================
 * 线程间通信 (IPC) —— 全部打开
 * ========================================================================== */
#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT              /* 事件集 */
#define RT_USING_MAILBOX            /* 邮箱 */
#define RT_USING_MESSAGEQUEUE       /* 消息队列 */

/* ==========================================================================
 * 软件定时器
 *   app_tasks.c 的按键扫描、以及 RT-Thread 自身的定时器都跑在这个线程上
 * ========================================================================== */
#define RT_USING_TIMER_SOFT
#define RT_TIMER_THREAD_PRIO        5
#define RT_TIMER_THREAD_STACK_SIZE  1024
#define RT_TIMER_TICK_PER_SECOND    100

/* ==========================================================================
 * 设备框架
 * ========================================================================== */
#define RT_USING_DEVICE
#define RT_USING_DEVICE_IPC

/* SPI: TDC-GP21 走 SPI3, 由 board/drv_spi.c 注册总线 */
#define RT_USING_SPI

/* PWM: 呼吸灯走 TIM3(PB0/PB1) + TIM4(PD12), 由 board/drv_pwm.c 注册 */
#define RT_USING_PWM
#define RT_USING_HWTIMER

/* PIN: 按键走 rt_pin_read, 由 board/drv_gpio.c 实现 rt_pin_ops */
#define RT_USING_PIN

/* SERIAL: USART1 注册成 "usart1" 设备, 由 board/drv_usart.c 实现。
 * msh/FINSH 需要它作为输入通道 —— 只覆盖 rt_hw_console_output() 只能输出,
 * 收不到字符, shell 就没法交互。 */
#define RT_USING_SERIAL
#define RT_USING_USART1

/* ==========================================================================
 * 控制台
 * ========================================================================== */
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE          128
#define RT_CONSOLE_DEVICE_NAME      "usart1"

/* ==========================================================================
 * 组件与启动流程
 * ========================================================================== */
#define RT_USING_COMPONENTS_INIT

/* 使用内核的用户 main 线程:
 *   板级启动 -> entry() -> rtthread_startup() -> 创建 main 线程
 *   -> main_thread_entry() -> rt_components_init() -> main()
 * 因此 board/rtthread_entry.c 里的 main() 是"用户主函数", 不负责启动内核。 */
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE   2048
/* main 线程优先级设低, 避免与 app_tasks.c 的 tdc/proc/lvgl (4/10/16) 抢占 */
#define RT_MAIN_THREAD_PRIORITY     25

/* ==========================================================================
 * FINSH / msh 命令行 —— 验证移植最直接的工具
 *   连上串口 115200 就能用:
 *     help              列出所有命令
 *     list_thread       看 tdc/proc/lvgl/tshell 线程与栈使用
 *     list_device       看 spi3 / spi30 / usart1 / pin 设备是否都注册上了
 *     list_timer        看软件定时器 (按键扫描)
 *     list_mem / free   看内核堆占用
 *     list_sem / list_mutex / list_event / list_mb / list_msgqueue / list_mempool
 *     ps                看线程状态
 *     version           看内核版本
 * ========================================================================== */
#define RT_USING_FINSH
#define FINSH_THREAD_NAME           "tshell"
#define FINSH_THREAD_PRIORITY       20
#define FINSH_THREAD_STACK_SIZE     4096
#define FINSH_USING_HISTORY
#define FINSH_HISTORY_LINES         5
#define FINSH_USING_SYMTAB
#define FINSH_USING_DESCRIPTION
#define FINSH_USING_MSH
#define FINSH_USING_MSH_DEFAULT     /* 上电直接进 msh, 不用先敲 msh 命令 */
#define FINSH_ARG_MAX               10
#define FINSH_CMD_SIZE              80

#endif /* __RTTCONFIG_H__ */
