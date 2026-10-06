#ifndef DRV_LCD_DEVICE_H
#define DRV_LCD_DEVICE_H
#include <rtdevice.h>
/**
 * @brief lcd0 图形设备控制接口，在线程中通过 rt_device_control() 使用。
 * @details 先 find/open；GET_INFO 的 arg 为 rt_device_graphic_info*，获得 240×320/RGB565 信息。
 *          LCD_CTRL_BLIT 的 arg 为 lcd_blit_t*：左上坐标 + 宽高，pixels 为连续逐行 RGB565 数据。
 *          设备同步发送像素，函数返回后可复用缓冲；矩形不可越界，像素数至少 width*height。
 *          LCD_CTRL_COLORBARS 的 arg 传 RT_NULL，绘制红绿蓝白黑五条横向色带。
 * @note 设备锁只保证总线事务不交错；使用 LVGL 时应由 LVGL 线程统一负责绘制。
 *       调试色条请使用 msh 的 lcd_test on/off，由界面线程处理暂停和恢复。
 */
#define LCD_CTRL_BLIT 0x200
#define LCD_CTRL_COLORBARS 0x201
typedef struct {
    rt_uint16_t x, y, width, height;
    const rt_uint16_t *pixels;
} lcd_blit_t;
#endif
