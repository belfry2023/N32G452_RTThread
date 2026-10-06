/**
 * @file    drv_pwm.h
 * @brief   RT-Thread PWM 设备驱动 (N32G452) —— TIM3 / TIM4
 *
 *  注册两个 PWM 设备:
 *      "pwm3"  TIM3   CH3 = PB0,  CH4 = PB1      (三路呼吸灯中的前两路)
 *      "pwm4"  TIM4   CH1 = PD12                 (需要打开 TIM4 重映射)
 *
 *  用法（走 RT-Thread 设备框架, 应用层不碰寄存器）:
 *      struct rt_device_pwm *pwm = (struct rt_device_pwm *)rt_device_find("pwm3");
 *      rt_pwm_set(pwm, 3, 1000000, 300000);   // 通道3, 周期1ms, 高电平0.3ms -> 30%
 *      rt_pwm_enable(pwm, 3);
 *
 *  ⚠️ channel 用的是【硬件通道号 1~4】, 和 TIMx_CHn 一一对应, 不是 0 起。
 *  ⚠️ period / pulse 单位是【纳秒】—— 这是 RT-Thread PWM 框架的约定。
 */
#ifndef __DRV_PWM_H__
#define __DRV_PWM_H__

#include <rtthread.h>
#include <rtdevice.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备名 */
#define PWM_DEV_TIM3        "pwm3"
#define PWM_DEV_TIM4        "pwm4"

/* 硬件通道号 —— 与 TIMx_CHn 一致 */
#define PWM_CH1             1
#define PWM_CH3             3
#define PWM_CH4             4

/**
 * @brief 初始化 TIM3/TIM4 并注册 pwm3、pwm4 设备。
 * @return 0=注册完成，否则返回设备注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动调用；应用用 rt_device_find() 找到设备后调用 rt_pwm_set()/enable()。
 *          动作：配置 GPIO，绑定硬件和内部对象，初始化时基，再依次注册两个原生 RT-Thread PWM 设备。
 */
int drv_pwm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_PWM_H__ */
