#include <rtthread.h>
#include <rtdevice.h>
#include "drv_lcd.h"
#include "drv_lcd_device.h"

static struct rt_device lcd;
static struct rt_mutex lcd_lock;
/**
 * @brief lcd0 的设备初始化回调，将底层屏幕初始化结果转换为 RT 错误码。
 * @param dev 图形设备对象，当前实现不使用该参数。
 * @return RT_EOK=初始化序列执行成功，-RT_EINVAL=配置检查未通过。
 * @details 用法：首次 rt_device_open() 时由框架调用，必须在线程中。
 *          动作：执行 Gc9307cInit()，其中包含复位、模组配置及等待时间。
 */
static rt_err_t lcd_init(rt_device_t dev)
{
    (void)dev;
    return Gc9307cInit() == LCD_OK ? RT_EOK : -RT_EINVAL;
}
/**
 * @brief 在同一把互斥锁内处理屏幕信息查询、矩形传输和色条测试。
 * @param dev lcd0；cmd 为 RTGRAPHIC_CTRL_GET_INFO/LCD_CTRL_BLIT/LCD_CTRL_COLORBARS；arg 为对应结构体或 RT_NULL。
 * @return RT_EOK=完成，-RT_EINVAL=参数/矩形无效，-RT_ENOSYS=未知命令。
 * @details 用法：打开设备后在线程中调用 rt_device_control()；BLIT 传 lcd_blit_t*，GET_INFO 传图形信息地址。
 *          动作：加锁后查询 RGB565/240×320 信息，或验证矩形并同步写像素，或画红绿蓝白黑五条色带。
 * @note 像素缓冲在函数返回前必须有效；互斥锁避免总线交错，但页面绘制归属仍应统一交给 LVGL 线程。
 */
static rt_err_t lcd_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    rt_err_t err = RT_EOK;
    rt_mutex_take(&lcd_lock, RT_WAITING_FOREVER);
    switch (cmd) {
    case RTGRAPHIC_CTRL_GET_INFO:
        if (!arg) { err = -RT_EINVAL; break; }
        *(struct rt_device_graphic_info *)arg = (struct rt_device_graphic_info){
            RTGRAPHIC_PIXEL_FORMAT_RGB565, 16, 0, LCD_WIDTH, LCD_HEIGHT, RT_NULL};
        break;
    case LCD_CTRL_BLIT: {
        const lcd_blit_t *b = arg;
        if (!b || !b->pixels || !b->width || !b->height ||
            (rt_uint32_t)b->x + b->width > LCD_WIDTH ||
            (rt_uint32_t)b->y + b->height > LCD_HEIGHT) { err = -RT_EINVAL; break; }
        Gc9307cFlush(b->x, b->y, b->x + b->width - 1, b->y + b->height - 1, b->pixels);
        break;
    }
    case LCD_CTRL_COLORBARS: {
        const uint16_t colors[] = {LCD_RED, LCD_GREEN, LCD_BLUE, LCD_WHITE, LCD_BLACK};
        for (unsigned i = 0; i < 5; ++i)
            Gc9307cFill(0, i * 64, LCD_WIDTH - 1, i * 64 + 63, colors[i]);
        break;
    }
    default: err = -RT_ENOSYS; break;
    }
    rt_mutex_release(&lcd_lock);
    return err;
}
/**
 * @brief 把 GC9307C 封装注册为 RT-Thread 图形设备 lcd0。
 * @return rt_device_register() 的结果，RT_EOK 表示注册成功。
 * @details 用法：INIT_DEVICE_EXPORT 自动执行一次，不在 main() 重复调用。
 *          动作：初始化屏幕锁、设置设备类型/init/control 回调，再注册只写设备；此时尚未发送屏幕初始化命令。
 */
static int lcd_register(void)
{
    rt_mutex_init(&lcd_lock, "lcd", RT_IPC_FLAG_PRIO);
    lcd.type = RT_Device_Class_Graphic;
    lcd.init = lcd_init;
    lcd.control = lcd_control;
    return rt_device_register(&lcd, "lcd0", RT_DEVICE_FLAG_WRONLY);
}
INIT_DEVICE_EXPORT(lcd_register);
