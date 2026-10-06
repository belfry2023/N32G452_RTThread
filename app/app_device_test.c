#include <rtthread.h>
#include <rtdevice.h>
#include "board_config.h"
static struct rt_semaphore test_sem;
static volatile unsigned test_count;
/**
 * @brief TIM6 到期通知：累加计数并唤醒测试线程。
 * @param dev 定时器设备；size 通知长度，均不使用。
 * @return 释放测试信号量的结果。
 * @details 用法：timer_test() 用 rt_device_set_rx_indicate() 注册。
 *          动作：在硬件中断上下文执行，只计数和释放信号量，不延时、不获取互斥锁。
 */
static rt_err_t timer_callback(rt_device_t dev, rt_size_t size)
{
    (void)dev; (void)size;
    test_count++;
    return rt_sem_release(&test_sem);
}
/**
 * @brief 通过 RT-Thread hwtimer 设备接口验证 TIM6 周期和单次模式。
 * @return RT_EOK 表示通过，找不到设备或控制/等待失败时返回负错误码。
 * @details 用法：在 msh 输入 timer_test，测试期间需独占 timer6，不能并发重复执行。
 *          动作：设为 1 MHz 计数、10 ms 周期，等待至少三次通知；再改为单次模式验证只触发一次。
 *          结束后关闭设备并移除测试信号量；具体耗时和计数打印到串口。
 */
static int timer_test(void)
{
    rt_device_t timer = rt_device_find("timer6");
    if (!timer) return -RT_ENOSYS;
    rt_err_t err = rt_device_open(timer, RT_DEVICE_OFLAG_RDWR);
    if (err != RT_EOK) return err;
    struct rt_hwtimer_info info;
    rt_uint32_t hz = 1000000;
    rt_hwtimer_mode_t mode = HWTIMER_MODE_PERIOD;
    rt_hwtimerval_t value = {0, 10000};
    test_count = 0;
    rt_sem_init(&test_sem, "tmtest", 0, RT_IPC_FLAG_FIFO);
    rt_device_set_rx_indicate(timer, timer_callback);
    err = rt_device_control(timer, HWTIMER_CTRL_INFO_GET, &info);
    if (err == RT_EOK) err = rt_device_control(timer, HWTIMER_CTRL_FREQ_SET, &hz);
    if (err == RT_EOK) err = rt_device_control(timer, HWTIMER_CTRL_MODE_SET, &mode);
    if (err == RT_EOK && rt_device_write(timer, 0, &value, sizeof(value)) != sizeof(value)) err = -RT_EIO;
    rt_tick_t begin = rt_tick_get();
    for (unsigned i = 0; i < 3 && err == RT_EOK; ++i)
        err = rt_sem_take(&test_sem, rt_tick_from_millisecond(100));
    rt_device_control(timer, HWTIMER_CTRL_STOP, RT_NULL);
    rt_kprintf("timer6 periodic: result=%d count=%u elapsed=%u ticks\n",
        (int)err, test_count, (unsigned)(rt_tick_get()-begin));
    if (err == RT_EOK) {
        while (rt_sem_take(&test_sem, 0) == RT_EOK) {}
        mode = HWTIMER_MODE_ONESHOT;
        err = rt_device_control(timer, HWTIMER_CTRL_MODE_SET, &mode);
        test_count = 0;
        if (err == RT_EOK && rt_device_write(timer, 0, &value, sizeof(value)) != sizeof(value)) err = -RT_EIO;
        if (err == RT_EOK) err = rt_sem_take(&test_sem, rt_tick_from_millisecond(100));
        rt_thread_mdelay(30);
        if (test_count != 1) err = -RT_ERROR;
        rt_kprintf("timer6 one-shot: result=%d count=%u\n", (int)err, test_count);
    }
    rt_device_close(timer);
    rt_sem_detach(&test_sem);
    return err;
}
MSH_CMD_EXPORT(timer_test, verify timer6 periodic and one-shot interrupts);
