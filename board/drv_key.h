/**
 * @file    drv_key.h
 * @brief   按键驱动 —— 3 按键 + 手势识别（供 LVGL 编码器输入使用）
 *
 *  按键布局（无触摸方案）:
 *      PREV   焦点移到上一个控件（编码器逆时针一格）
 *      NEXT   焦点移到下一个控件（编码器顺时针一格）
 *      ENTER  确认 / 长按进编辑 / 两次短按等组合手势
 *
 *  手势识别逻辑本身在 board/key_gesture.c（纯逻辑、可离线自测）,
 *  本文件只负责: 读引脚 -> 消抖 -> 喂给状态机 -> 对外提供查询接口。
 */
#ifndef __DRV_KEY_H__
#define __DRV_KEY_H__

#include <rtthread.h>

#include "key_gesture.h"

/* 按键编号 */
typedef enum
{
    KEY_PREV = 0,       /* 上一个 */
    KEY_NEXT,           /* 下一个 */
    KEY_ENTER,          /* 确认   */
    KEY_MAX
} key_id_t;

/**
 * @brief 初始化三路按键 GPIO、手势上下文和扫描软件定时器。
 * @return 当前实现执行完返回 RT_EOK。
 * @details 用法：由 INIT_DEVICE_EXPORT 自动调用一次；接线修改 board_config.h，手感参数修改本文件 KEY_* 宏。
 *          动作：配置上拉输入，清消抖状态，设置方向键/确认键的组合窗口，启动 10 ms 周期扫描。
 * @note 按键另一端接 GND；不要再次手动初始化仍在运行的定时器。
 */
int key_init(void);

/**
 * @brief 从指定按键的 FIFO 取走一个已确认手势。
 * @param id KEY_PREV、KEY_NEXT 或 KEY_ENTER。
 * @return 手势枚举；队列空或编号无效时返回 KEY_GESTURE_NONE。
 * @details 用法：一般由 LVGL 输入线程读取 ENTER；读取会消费事件，不能让多个模块重复取同一个队列。
 *          动作：检查编号后调用 key_gesture_take()；PREV/NEXT 队列已由 key_take_encoder_diff() 消费。
 */
key_gesture_t key_take_gesture(key_id_t id);

/**
 * @brief 查询按键识别器当前的长按活动标志。
 * @param id 按键枚举值。
 * @return 非 0=当前状态机报告长按活动；0=未活动或编号无效。
 * @details 用法：PREV/NEXT 长按连发使用该即时标志，不必等最终手势出队。
 *          动作：只读内部状态；不会消费队列，组合手势的锁定状态可能清除此标志。
 */
rt_uint8_t key_is_long_active(key_id_t id);

/**
 * @brief 查询按键消抖后的当前电平状态。
 * @param id 按键枚举值。
 * @return 1=按下，0=松开或编号无效。
 * @details 用法：用于界面的按下/松开视觉反馈，不等同于短按事件。
 *          动作：返回最近一轮扫描的稳定状态，不额外读取 GPIO，不消费手势。
 */
rt_uint8_t key_is_pressed(key_id_t id);

/**
 * @brief 把 PREV/NEXT 积累的手势取走并转换为带符号步数。
 * @return NEXT 为正、PREV 为负；没有方向事件时为 0。
 * @details 用法：由 LVGL 输入线程单独消费，其他模块不要同时读取这两个按键的手势队列。
 *          动作：短按计一格、双短按计两格；长按不在这里计步，由 encoder_repeat() 实现持续滚动。
 */
rt_int32_t key_take_encoder_diff(void);

/**
 * @brief 用合成电平/时间序列验证手势状态机。
 * @return 0=全部通过，非 0=失败的用例数。
 * @details 用法：msh 输入 keytest，或由启动自检调用；不需要真实按键。
 *          动作：运行 key_gesture_selftest.h 中共享用例，参数沿用当前长按、组合窗口和扫描周期。
 */
int key_selftest(void);

#endif /* __DRV_KEY_H__ */
