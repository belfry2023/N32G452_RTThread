/**
 * @file    rtthread_entry.c
 * @brief   RT-Thread 内核入口与用户主函数
 *
 *  启动链路（GCC）:
 *
 *      复位
 *       └─ startup_n32g45x_rtthread.s : Reset_Handler
 *            └─ SystemInit()           (system_n32g45x.c, 配时钟树)
 *            └─ __libc_init_array()
 *            └─ entry()                ← RT 内核 src/components.c 提供
 *                 └─ rtthread_startup()  内核入口, 不返回
 *                      └─ rt_hw_board_init()          (board/board.c)
 *                      └─ rt_application_init()       创建 main 线程
 *                      └─ rt_system_scheduler_start()
 *                           └─ main 线程: main_thread_entry()
 *                                └─ rt_components_init()  ← 触发 INIT_APP_EXPORT
 *                                └─ main()                ← 本文件, 用户主函数
 *
 *  ⚠️ 注意区分两个东西:
 *      entry()  —— 内核入口, 只调用一次, 不返回
 *      main()   —— 用户主函数, 在内核的 main 线程里运行, 返回后该线程退出
 *
 *  GCC 启动文件必须调用 entry()；若直接跳到用户 main()，会绕过内核初始化。
 *  Keil AC5/AC6 的链路是 Reset_Handler -> SystemInit -> Arm C 库 __main，
 *  再由 RT 内核的 $Sub$$main 包装进入 rtthread_startup()；main 线程最终
 *  调用 $Super$$main，即本文件中的用户 main()。
 */

#include <rtthread.h>

/* ==========================================================================
 *  用户主函数 —— 在 main 线程里运行
 *
 *  ⚠️ 这里【不】定义 entry():
 *     RT-Thread 内核的 src/components.c 在 __GNUC__ 分支里已经提供了
 *         int entry(void) { rtthread_startup(); return 0; }
 *     自己再定义一个会在链接期报 multiple definition。
 *
 *  这里**不应该**做初始化的重活:
 *      板级外设      -> board/board.c + drv_spi.c (INIT_BOARD_EXPORT)
 *      应用线程/界面 -> app/app_tasks.c           (INIT_APP_EXPORT)
 *  板级项由 rt_hw_board_init() 中的 rt_components_board_init() 执行；
 *  组件/应用项由 main 线程中的 rt_components_init() 执行，无需手动触发。
 *
 *  main 线程返回后会自动退出, 把 CPU 让给应用线程。
 * ========================================================================== */
/**
 * @brief 用户主函数：在 RT-Thread 的 main 线程中打印启动信息。
 * @return 0；返回后只结束 main 线程，采集、界面、按键等任务继续运行。
 * @details 用法：可在这里添加一次性的用户初始化；不要再次启动内核或重复调用 app_tasks_init()。
 *          动作：进入本函数前，内核已完成板级初始化、启动调度，并执行组件/应用自动初始化。
 * @note 持续业务优先放在 app/ 的线程中；若在此添加循环，需用延时或 IPC 等待让出 CPU。
 */
int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("========================================\n");
    rt_kprintf("  N32G452  RT-Thread\n");
    rt_kprintf("  tick     : %d Hz\n", RT_TICK_PER_SECOND);
    rt_kprintf("  priority : %d levels\n", RT_THREAD_PRIORITY_MAX);
    rt_kprintf("========================================\n");

    return 0;
}
