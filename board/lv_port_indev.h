/**
 * @file    lv_port_indev.h
 * @brief   LVGL 输入设备移植层 —— 3 按键模拟编码器 + 手势识别 (无触摸)
 */
#ifndef __LV_PORT_INDEV_H__
#define __LV_PORT_INDEV_H__

#include "lvgl.h"
#include "key_gesture.h"

/**
 * @brief 注册按键模拟编码器并创建默认焦点组。
 * @details 用法：lv_init() 之后在 LVGL 线程调用一次；按键 GPIO/扫描由 key_init() 自动初始化。
 *          动作：绑定 indev_read()，创建并设置默认 group，把编码器挂到该组并开启循环导航。
 * @note 控件需加入 group 才能被按键选择；本函数没有重复初始化保护。
 */
void lv_port_indev_init(void);

/**
 * @brief 取得已注册的 LVGL 编码器输入对象。
 * @return 对象指针；初始化前为 RT_NULL，由 LVGL 管理其生命周期。
 * @details 用法：在 LVGL 线程查询或配置输入设备，调用者不要自行释放。
 *          动作：返回保存的 s_indev，不执行硬件读取。
 */
lv_indev_t *lv_port_indev_get(void);

/**
 * @brief 取得按键导航使用的默认控件组。
 * @return 组对象指针；初始化前为 RT_NULL。
 * @details 用法：在 LVGL 线程使用 lv_group_add_obj(lv_port_indev_get_group(), obj) 添加可操作控件。
 *          动作：返回 s_group；页面切换时需维护组内对象，使焦点只落在可见控件上。
 */
lv_group_t *lv_port_indev_get_group(void);

/**
 * @brief  组合手势回调
 * @param  g  KEY_GESTURE_DOUBLE / LONG_SHORT / SHORT_LONG
 *
 *  短按与长按由本文件直接派发给 LVGL（复刻原生编码器行为），不走回调；
 *  这三种"组合手势"LVGL 原生不支持，交给应用决定做什么。
 *
 *  ⚠️ 回调在 lvgl 线程里、由 lv_timer_handler() 调用，可以安全操作 LVGL 对象。
 */
typedef void (*lv_port_indev_gesture_cb_t)(key_gesture_t g);

/**
 * @brief 注册应用的 ENTER 组合手势回调。
 * @param cb 回调函数，传 RT_NULL 取消回调。
 * @details 用法：在 LVGL 线程初始化或切换页面模式时设置；回调也在 LVGL 线程执行。
 *          动作：保存函数指针，双短按/长接短/短接长时转交应用；普通短按和长按仍由本移植层派发。
 */
void lv_port_indev_set_gesture_cb(lv_port_indev_gesture_cb_t cb);

#endif /* __LV_PORT_INDEV_H__ */
