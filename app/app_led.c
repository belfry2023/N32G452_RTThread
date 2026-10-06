/**
 * @file    app_led.c
 * @brief   呼吸灯实现 —— 三路硬件 PWM + 余弦呼吸曲线
 *
 *  ── 呼吸曲线为什么不用正弦算 ──
 *  呼吸的自然感来自"两头慢、中间快", 也就是 (1 - cos θ) / 2 这条曲线。
 *  在 MCU 上实时算 cos 既费时又要链数学库, 所以直接查一张 32 项的
 *  余弦表 —— 32 级在慢速渐变下肉眼看不出台阶, 而且查表是零开销。
 *
 *  ── 三路为什么要错相位 ──
 *  三个灯同时亮到最亮、同时灭, 看起来像"一闪一闪";
 *  错开相位才像呼吸。相位偏移写在下表里, 改它就能调效果。
 *
 *  ── 为什么单独一个线程 ──
 *  呼吸灯是纯"装饰"功能, 绝不能影响采样和刷屏。
 *  所以它优先级最低, 每 30ms 才动一次, CPU 占用可以忽略。
 */

#include <stdlib.h>     /* atoi */

#include <rtthread.h>
#include <rtdevice.h>

#include "drv_pwm.h"
#include "app_led.h"

/* ==========================================================================
 *  参数
 * ========================================================================== */
#define LED_PWM_PERIOD_NS   1000000u    /* 1 ms -> PWM 频率 1 kHz */
#define LED_BREATH_STEP_MS  30u         /* 每 30ms 更新一次占空比 */
#define LED_THREAD_PRIO     20          /* 比 lvgl(16) 还低: 纯装饰 */
#define LED_THREAD_STACK    512u
#define LED_THREAD_TICK     10

/* 32 项余弦表: duty(%) = (1 - cos(2*pi*i/32)) / 2 * 100 */
#define LED_LUT_LEN         32
static const rt_uint8_t s_breath_lut[LED_LUT_LEN] =
{
      0,   1,   4,   8,  15,  22,  31,  40,
     50,  60,  69,  78,  85,  92,  96,  99,
    100,  99,  96,  92,  85,  78,  69,  60,
     50,  40,  31,  22,  15,   8,   4,   1
};

/* ==========================================================================
 *  灯的接线表   ← 改接线只改这里
 * ========================================================================== */
struct led_desc
{
    const char *name;
    const char *pwm_dev;
    rt_uint8_t  channel;
    rt_uint8_t  phase;      /* 呼吸相位偏移, 让三个灯错开 */
};

static const struct led_desc s_leds[APP_LED_MAX] =
{
    { "LED1", PWM_DEV_TIM3, PWM_CH3,  0  },     /* PB0  */
    { "LED2", PWM_DEV_TIM3, PWM_CH4, 11  },     /* PB1  */
    { "LED3", PWM_DEV_TIM4, PWM_CH1, 21  },     /* PD12 */
};

/* ==========================================================================
 *  状态
 * ========================================================================== */
static struct rt_device_pwm *s_pwm[APP_LED_MAX];
static rt_uint8_t            s_manual[APP_LED_MAX];   /* 1 = 手动指定, 不参与呼吸 */
static rt_uint8_t            s_duty[APP_LED_MAX];     /* 当前亮度 0~100 */

static volatile rt_uint8_t   s_breath_on = 1u;

static struct rt_thread s_led_thread;
static rt_uint8_t       s_led_stack[LED_THREAD_STACK];

/* ==========================================================================
 *  底层: 设某一路占空比
 * ========================================================================== */
/**
 * @brief 把一路 LED 的亮度百分比转换为 PWM 高电平时间。
 * @param led 已验证的 APP_LED_1..APP_LED_3；percent 为 0..100，超过 100 会限制为 100。
 * @details 用法：内部辅助函数，调用者先保证 led 编号合法。
 *          动作：保存当前占空比；设备存在时按固定 1 ms 周期计算纳秒脉宽并调用 rt_pwm_set()。
 */
static void led_write(app_led_t led, rt_uint8_t percent)
{
    if (percent > 100u)
    {
        percent = 100u;
    }

    s_duty[led] = percent;

    if (s_pwm[led] != RT_NULL)
    {
        /* pulse 单位纳秒: 1% = 10us = 10000ns */
        rt_pwm_set(s_pwm[led], s_leds[led].channel,
                   LED_PWM_PERIOD_NS, (rt_uint32_t)percent * (LED_PWM_PERIOD_NS / 100u));
    }
}

/* ==========================================================================
 *  呼吸线程
 * ========================================================================== */
/**
 * @brief 用查表方式持续生成三路错相的呼吸灯亮度。
 * @param parameter 线程参数，未使用。
 * @details 用法：由 app_led_init() 创建，不能从其他线程直接调用这个无限循环。
 *          动作：呼吸开启时更新非手动通道并推进相位，每 LED_BREATH_STEP_MS 毫秒让出 CPU。
 */
static void led_thread_entry(void *parameter)
{
    rt_tick_t  next  = rt_tick_get();
    rt_uint32_t phase = 0;
    int i;

    (void)parameter;

    for (;;)
    {
        if (s_breath_on != 0u)
        {
            for (i = 0; i < APP_LED_MAX; i++)
            {
                if (s_manual[i] == 0u)
                {
                    rt_uint8_t v = s_breath_lut[(phase + s_leds[i].phase) % LED_LUT_LEN];
                    led_write((app_led_t)i, v);
                }
            }
            phase++;
        }

        rt_thread_delay_until(&next, rt_tick_from_millisecond(LED_BREATH_STEP_MS));
    }
}

/* ==========================================================================
 *  对外接口
 * ========================================================================== */
/**
 * @brief 手动设置一路 LED 亮度，并使该路退出自动呼吸。
 * @param led APP_LED_1..APP_LED_3 枚举值；percent 为 0..100 的亮度百分比。
 * @details 用法：初始化完成后在线程中调用，例如 app_led_set(APP_LED_1, 30)。
 *          动作：设置该路手动标志，再更新 PWM；重新调用 app_led_breath(1) 会让全部通道恢复呼吸。
 * @note 调用方应传有效枚举值；此接口没有为多线程同时修改 LED 状态提供互斥保护。
 */
void app_led_set(app_led_t led, rt_uint8_t percent)
{
    if (led >= APP_LED_MAX)
    {
        return;
    }
    s_manual[led] = 1u;         /* 手动指定后就不再被呼吸覆盖 */
    led_write(led, percent);
}

/**
 * @brief 开启或暂停三路 LED 的自动呼吸。
 * @param on 0=暂停，非 0=开启。
 * @details 用法：在线程中调用 app_led_breath(0/1)。
 *          动作：暂停时保持当前亮度；开启时清除所有手动标志，由呼吸线程在下一轮更新亮度。
 */
void app_led_breath(rt_uint8_t on)
{
    int i;

    s_breath_on = (on != 0u) ? 1u : 0u;

    if (s_breath_on == 0u)
    {
        return;
    }

    /* 重新开呼吸: 清掉所有手动标记 */
    for (i = 0; i < APP_LED_MAX; i++)
    {
        s_manual[i] = 0u;
    }
}

/**
 * @brief 查询全局呼吸效果是否开启。
 * @return 1=开启，0=暂停；单路可能仍处于手动模式。
 * @details 用法：界面初始化开关状态时调用。
 *          动作：直接读取全局开关，不访问 PWM 寄存器。
 */
rt_uint8_t app_led_breath_is_on(void)
{
    return s_breath_on;
}

/**
 * @brief 绑定三路 PWM 设备并创建呼吸灯线程。
 * @return 找不到 PWM 设备时返回 -RT_ERROR，正常执行完返回 0。
 * @details 用法：INIT_APP_EXPORT 自动执行一次；改灯的设备/通道关系使用 s_leds 表。
 *          动作：查找 pwm3/pwm4，先以零亮度配置并使能输出，再启动使用静态栈的低优先级线程。
 */
int app_led_init(void)
{
    int i;

    for (i = 0; i < APP_LED_MAX; i++)
    {
        s_pwm[i]    = (struct rt_device_pwm *)rt_device_find(s_leds[i].pwm_dev);
        s_manual[i] = 0u;
        s_duty[i]   = 0u;

        if (s_pwm[i] == RT_NULL)
        {
            rt_kprintf("[led] 找不到 PWM 设备 %s (灯 %s)\n",
                       s_leds[i].pwm_dev, s_leds[i].name);
            return -RT_ERROR;
        }

        /* 先把通道配好再使能, 避免上电瞬间满亮 */
        rt_pwm_set(s_pwm[i], s_leds[i].channel, LED_PWM_PERIOD_NS, 0u);
        rt_pwm_enable(s_pwm[i], s_leds[i].channel);
    }

    rt_thread_init(&s_led_thread, "led", led_thread_entry, RT_NULL,
                   s_led_stack, sizeof(s_led_stack),
                   LED_THREAD_PRIO, LED_THREAD_TICK);
    rt_thread_startup(&s_led_thread);

    rt_kprintf("[led] 3 路呼吸灯: %s(CH%d) %s(CH%d) %s(CH%d), "
               "PWM 1kHz, 每 %dms 更新\n",
               s_leds[0].name, s_leds[0].channel,
               s_leds[1].name, s_leds[1].channel,
               s_leds[2].name, s_leds[2].channel,
               LED_BREATH_STEP_MS);
    return 0;
}
INIT_APP_EXPORT(app_led_init);

/* ==========================================================================
 *  msh 命令:  led [breath on|off] | led set <0-2> <0-100>
 * ========================================================================== */
#ifdef RT_USING_FINSH
#include <finsh.h>

/**
 * @brief 串口命令 led：查询状态、开关呼吸或手动设置亮度。
 * @param argc 参数数量；argv 为 msh 分词后的命令参数。
 * @return 正常处理返回 0，手动设置的灯编号越界返回 -1。
 * @details 用法：led、led breath on/off，或 led set 0 30；亮度请传 0..100 的整数。
 *          动作：解析命令并调用应用 LED 接口，无设置参数时打印三路设备、通道和亮度。
 */
static int cmd_led(int argc, char **argv)
{
    int i;

    if ((argc >= 3) && (rt_strcmp(argv[1], "breath") == 0))
    {
        rt_uint8_t on = (rt_strcmp(argv[2], "on") == 0) ? 1u : 0u;
        app_led_breath(on);
        rt_kprintf("呼吸: %s\n", on ? "开" : "关");
        return 0;
    }

    if ((argc >= 4) && (rt_strcmp(argv[1], "set") == 0))
    {
        int idx = atoi(argv[2]);
        int pct = atoi(argv[3]);
        if ((idx < 0) || (idx >= APP_LED_MAX))
        {
            rt_kprintf("灯编号 0~%d\n", APP_LED_MAX - 1);
            return -1;
        }
        app_led_set((app_led_t)idx, (rt_uint8_t)pct);
        rt_kprintf("%s -> %d%%\n", s_leds[idx].name, pct);
        return 0;
    }

    rt_kprintf("呼吸: %s\n", s_breath_on ? "开" : "关");
    for (i = 0; i < APP_LED_MAX; i++)
    {
        rt_kprintf("  [%d] %-5s %-5s CH%d  %3d%%%s\n",
                   i, s_leds[i].name, s_leds[i].pwm_dev, s_leds[i].channel,
                   (unsigned)s_duty[i], s_manual[i] ? "  (手动)" : "");
    }
    rt_kprintf("用法: led breath on|off      led set <0-%d> <0-100>\n",
               APP_LED_MAX - 1);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_led, led, PWM breathing LED control);
#endif
