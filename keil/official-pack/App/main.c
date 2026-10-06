/**
 * @file main.c
 * @brief 官方 Pack 工程的用户入口，位于 RT-Thread main 线程中。
 * 启动链：官方 Reset_Handler -> SystemInit -> Arm __main -> RT 内核包装 -> 用户 main。
 * 引脚改工作区 inc/board_config.h，采集/界面改工作区 app/app_tasks.c。
 */
#include <rtthread.h>

/**
 * @brief 执行一次性的用户初始化并打印启动信息。
 * @return 0；只结束 main 线程，采集、界面、按键和 LED 线程继续工作。
 * @details 用法：在这里添加自己的启动设置；持续业务放到 app/ 中的线程。
 *          动作：设备和应用 INIT_* 初始化项已自动执行，本函数无需再次调用 app_tasks_init()。
 * @note LVGL 对象只能在界面线程中操作；如果添加循环，应通过延时或 IPC 等待让出 CPU。
 */
int main(void)
{
    rt_kprintf("\nN32G452 / Official NSING Pack / RT-Thread 3.1.4\n");
    rt_kprintf("HSI PLL 128 MHz, GP21 reference 5 MHz, GC9307C 240x320\n");
    return 0;
}
