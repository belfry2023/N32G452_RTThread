/**
 * @file    app_led.h
 * @brief   呼吸灯 —— 用 PWM 设备驱动三路 LED
 *
 *  接线（与 board/drv_pwm.c 对应）:
 *      LED1  PB0   pwm3 通道3   TIM3_CH3
 *      LED2  PB1   pwm3 通道4   TIM3_CH4
 *      LED3  PD12  pwm4 通道1   TIM4_CH1
 *
 *  这一层只调 RT-Thread 的 PWM 设备 API（rt_pwm_set / rt_pwm_enable）,
 *  不碰任何寄存器 —— 换板子时这里不用改, 只改 board/drv_pwm.c。
 */
#ifndef __APP_LED_H__
#define __APP_LED_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_LED_1 = 0,      /* PB0  */
    APP_LED_2,          /* PB1  */
    APP_LED_3,          /* PD12 */
    APP_LED_MAX
} app_led_t;

/**
 * @brief 绑定三路 PWM 设备并创建呼吸灯线程。
 * @return 找不到 PWM 设备时返回 -RT_ERROR，正常执行完返回 0。
 * @details 用法：INIT_APP_EXPORT 自动执行一次；改灯的设备/通道关系使用 s_leds 表。
 *          动作：查找 pwm3/pwm4，先以零亮度配置并使能输出，再启动使用静态栈的低优先级线程。
 */
int app_led_init(void);

/**
 * @brief 手动设置一路 LED 亮度，并使该路退出自动呼吸。
 * @param led APP_LED_1..APP_LED_3 枚举值；percent 为 0..100 的亮度百分比。
 * @details 用法：初始化完成后在线程中调用，例如 app_led_set(APP_LED_1, 30)。
 *          动作：设置该路手动标志，再更新 PWM；重新调用 app_led_breath(1) 会让全部通道恢复呼吸。
 * @note 调用方应传有效枚举值；此接口没有为多线程同时修改 LED 状态提供互斥保护。
 */
void app_led_set(app_led_t led, rt_uint8_t percent);

/**
 * @brief 开启或暂停三路 LED 的自动呼吸。
 * @param on 0=暂停，非 0=开启。
 * @details 用法：在线程中调用 app_led_breath(0/1)。
 *          动作：暂停时保持当前亮度；开启时清除所有手动标志，由呼吸线程在下一轮更新亮度。
 */
void app_led_breath(rt_uint8_t on);

/**
 * @brief 查询全局呼吸效果是否开启。
 * @return 1=开启，0=暂停；单路可能仍处于手动模式。
 * @details 用法：界面初始化开关状态时调用。
 *          动作：直接读取全局开关，不访问 PWM 寄存器。
 */
rt_uint8_t app_led_breath_is_on(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LED_H__ */
