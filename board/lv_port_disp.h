/**
 * @file    lv_port_disp.h
 * @brief   LVGL 显示接口移植层 —— 绑定 GC9307C 8080 并口屏
 */
#ifndef __LV_PORT_DISP_H__
#define __LV_PORT_DISP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drv_lcd.h"        /* LCD_WIDTH / LCD_HEIGHT, 由板级驱动给出 */

/* ==========================================================================
 *  显示缓冲行数
 *    LVGL 官方建议至少为屏幕的 1/10; 这里用 240x40 = 屏幕的 1/8。
 *    单个缓冲 240 * 40 * 2 = 19200 字节 = 18.75KB
 *    放在头文件里是为了让 msh 命令 lvgl 能打印出来。
 * ========================================================================== */
#define DISP_BUF_LINES      40u

/**
 * @brief 打开 lcd0 并注册 LVGL 显示驱动和单个绘图缓冲。
 * @return RT_EOK=成功，-RT_ERROR=设备打开失败，-RT_ENOMEM=LVGL 注册失败。
 * @details 用法：lv_init() 之后，在 LVGL 线程内调用一次，当前由 ui_init() 负责。
 *          动作：打开屏幕，绑定 240×DISP_BUF_LINES 的 RGB565 缓冲，设置分辨率与 flush_cb 并注册。
 * @note 缓冲行数在 lv_port_disp.h 修改；当前同步传输使用单缓冲，显示注册失败时会关闭设备。
 */
int lv_port_disp_init(void); /* 0 on success, negative on failure */

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_DISP_H__ */
