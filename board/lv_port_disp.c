/**
 * @file    lv_port_disp.c
 * @brief   LVGL 显示接口移植层 —— 绑定到 GC9307C 8080 并口驱动
 *
 *  LVGL 移植只需要实现一个回调: flush_cb
 *      LVGL 渲染完一块脏矩形后调用它, 我们把这块像素推给屏幕。
 *
 *  ⚠️ 当前 LCD 驱动是 CPU 直写(阻塞), flush_cb 返回时数据已全部送出,
 *     所以可以立刻调 lv_disp_flush_ready()。
 *     也正因为是阻塞式, 双缓冲拿不到"渲染与刷屏重叠"的收益, 这里只用单缓冲,
 *     省下 19.2KB RAM。详见 docs/BRINGUP_GP21.md。
 */

#include <rtthread.h>
#include "lvgl.h"

#include "drv_lcd.h"
#include "drv_lcd_device.h"
static rt_device_t s_lcd;
#include "lv_port_disp.h"

/* ==========================================================================
 *  显示缓冲
 *    DISP_BUF_LINES 定义在 lv_port_disp.h (msh 命令要用到)
 *    单个缓冲 240 * 40 * 2 = 19200 字节 = 18.75KB
 * ========================================================================== */
#define DISP_BUF_PIXELS     (LCD_WIDTH * DISP_BUF_LINES)

static lv_color_t         s_disp_buf[DISP_BUF_PIXELS];
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;

/**
 * @brief 把 LVGL 渲染好的矩形像素交给 lcd0 设备同步发送。
 * @param drv LVGL 显示驱动；area 为包含端点的刷新区域；px_map 为相应连续 RGB565 像素。
 * @details 用法：注册为 flush_cb，由 LVGL 线程调用，应用不需要主动调用。
 *          动作：将区域转换为 lcd_blit_t，执行 LCD_CTRL_BLIT，再调用 lv_disp_flush_ready() 归还缓冲。
 * @note 当前没有 DMA，返回前总线传输已完成；若以后改异步 DMA，必须把 ready 通知移到实际传输完成后。
 */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
    lcd_blit_t blit = {(rt_uint16_t)area->x1, (rt_uint16_t)area->y1,
        (rt_uint16_t)(area->x2-area->x1+1), (rt_uint16_t)(area->y2-area->y1+1),
        (const rt_uint16_t *)px_map};
    if (s_lcd) rt_device_control(s_lcd, LCD_CTRL_BLIT, &blit);

    /* 阻塞式刷屏: 走到这里数据已全部写出, 可以立即通知 LVGL 继续 */
    lv_disp_flush_ready(drv);
}

/**
 * @brief 打开 lcd0 并注册 LVGL 显示驱动和单个绘图缓冲。
 * @return RT_EOK=成功，-RT_ERROR=设备打开失败，-RT_ENOMEM=LVGL 注册失败。
 * @details 用法：lv_init() 之后，在 LVGL 线程内调用一次，当前由 ui_init() 负责。
 *          动作：打开屏幕，绑定 240×DISP_BUF_LINES 的 RGB565 缓冲，设置分辨率与 flush_cb 并注册。
 * @note 缓冲行数在 lv_port_disp.h 修改；当前同步传输使用单缓冲，显示注册失败时会关闭设备。
 */
int lv_port_disp_init(void)
{
    s_lcd = rt_device_find("lcd0");
    if (!s_lcd || rt_device_open(s_lcd, RT_DEVICE_OFLAG_WRONLY) != RT_EOK) {
        rt_kprintf("[lcd] initialization failed\n");
        s_lcd = RT_NULL;
        return -RT_ERROR;
    }
    /* 单缓冲: buf_2 传 RT_NULL */
    lv_disp_draw_buf_init(&s_draw_buf, s_disp_buf, RT_NULL, DISP_BUF_PIXELS);

    lv_disp_drv_init(&s_disp_drv);

    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.flush_cb = disp_flush;

    if (!lv_disp_drv_register(&s_disp_drv)) {
        rt_device_close(s_lcd);
        s_lcd = RT_NULL;
        return -RT_ENOMEM;
    }
    return RT_EOK;
}
