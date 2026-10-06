/**
 * @file    main.c
 * @brief   用户主函数 —— 【这份代码属于你的工程, 随便改】
 *
 *  在 pack(只读) 与你的工程(可改) 之间的分工:
 *
 *      pack 提供 (只读, 随包更新):
 *          - RT-Thread 3.1.4 内核、设备框架、IPC、FINSH/msh
 *          - BSP 驱动: PIN / USART1 / PWM / TIM6 / SPI3 / 按键 / 启动文件 / 分散加载
 *          - 器件抽象: GP21 (TDC)、GC9307C (LCD)、LVGL
 *
 *      你的工程负责 (可改):
 *          - 这个 main.c —— 用户主函数
 *          - 你自己新建的 app_xxx.c —— 业务逻辑
 *
 *  ── 关于 main() 的注意事项 ──
 *  你【不应该】自己定义 entry():
 *      RT-Thread 内核的 src/components.c 已经提供了
 *          int entry(void) { rtthread_startup(); return 0; }
 *      自己再定义一个会在链接期报 multiple definition。
 *
 *  启动链路:
 *      复位 -> startup : Reset_Handler
 *           -> SystemInit()
 *           -> entry()                 内核入口, 不返回
 *                -> rt_hw_board_init()        板级: 时钟/堆/控制台/INIT_BOARD_EXPORT
 *                -> rt_application_init()      创建 main 线程
 *                -> rt_system_scheduler_start()
 *                     -> main 线程: main_thread_entry()
 *                          -> rt_components_init()   触发 INIT_DEVICE/APP_EXPORT
 *                          -> main()                 ← 就是本函数
 *
 *  ⚠️ main() 运行在内核的 main 线程里, 优先级 RT_MAIN_THREAD_PRIORITY。
 *     **这里不要写死循环, 也不要放耗时的初始化** —— 返回即可, 该线程会退出,
 *     CPU 让给应用线程。重活请用 INIT_*_EXPORT 或自己创建线程。
 */

#include <rtthread.h>

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("========================================\n");
    rt_kprintf("  test01  N32G452  RT-Thread\n");
    rt_kprintf("  tick     : %d Hz\n", RT_TICK_PER_SECOND);
    rt_kprintf("  priority : %d levels\n", RT_THREAD_PRIORITY_MAX);
    rt_kprintf("  在 msh 里敲 help 看可用命令\n");
    rt_kprintf("========================================\n");

    /* ----------------------------------------------------------------------
     *  你的应用代码从这里开始。
     *
     *  两种常见做法:
     *
     *  1) 直接在 main 里建线程 (简单, 但线程栈要自己分配)
     *         rt_thread_t t = rt_thread_create("myapp", my_entry, RT_NULL,
     *                                          1024, 20, 10);
     *         if (t) rt_thread_startup(t);
     *
     *  2) 新建 app_xxx.c, 用 INIT_APP_EXPORT 注册初始化函数 (推荐)
     *         static int my_init(void) { ...建线程...; return 0; }
     *         INIT_APP_EXPORT(my_init);
     *     这样不用动 main.c, 加文件即可 —— 记得在 Keil 里把这个 .c
     *     添加到工程的某个 Group 才会参与编译。
     * ---------------------------------------------------------------------- */

    return 0;
}
