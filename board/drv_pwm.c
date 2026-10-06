/**
 * @file    drv_pwm.c
 * @brief   RT-Thread PWM 设备驱动 (N32G452) —— TIM3 (PB0/PB1) + TIM4 (PD12)
 *
 *  ── 为什么这三根脚能做硬件 PWM ──
 *  数据手册复用功能表（已逐条核对）:
 *      PB0  ->  TIM3_CH3                    无需重映射
 *      PB1  ->  TIM3_CH4                    无需重映射
 *      PD12 ->  TIM4_CH1                    需要开 TIM4 重映射
 *      PD13 ->  TIM4_CH2   ← 但 PD13 被 TDC 的 INT 占用, 所以这里不用
 *
 *  ⚠️ 原来脚手架里的 LED 在 PA5。PA5 的复用功能是
 *     SPI1_SCK / DAC_OUT2 / ADC2_IN2 / QSPI_SCK / I2C2_SDA —— 没有任何定时器通道,
 *     做不了硬件 PWM。所以呼吸灯必须挪到上面这三根脚上。
 *
 *  ── 时钟 ──
 *  TIM3/TIM4 时钟通过 RCC 查询，并处理 APB 定时器倍频。
 *  HSI/PLL 128MHz 下 TIMCLK=64MHz，预分频63后为1MHz。
 *  16 位 ARR -> 周期范围 1 us ~ 65.5 ms（即 15 Hz ~ 1 MHz）。
 *
 *  ── 为什么不用 DMA ──
 *  呼吸灯只是慢速改占空比（每几十毫秒一次）, CPU 开销可以忽略,
 *  用不上 DMA 搬运比较值。
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "n32g45x.h"
#include "n32g45x_rcc.h"
#include "n32g45x_gpio.h"
#include "n32g45x_tim.h"

#include "drv_pwm.h"
#include "board_config.h"

/* ==========================================================================
 *  时钟与分辨率
 * ========================================================================== */
#define PWM_NS_PER_TICK     1000u           /* 1 tick = 1us = 1000ns */
#define PWM_MAX_ARR         65535u

/* ==========================================================================
 *  设备结构
 * ========================================================================== */
#define PWM_CH_MAX          4

struct n32_pwm
{
    struct rt_device_pwm parent;
    TIM_Module          *tim;
    rt_uint16_t          arr;                    /* 当前自动重装值 */
    rt_uint16_t          ccr[PWM_CH_MAX + 1];    /* 各通道比较值, 下标 1~4 */
    rt_uint8_t           out_en[PWM_CH_MAX + 1]; /* 各通道输出使能 */
};

static struct n32_pwm s_pwm3;
static struct n32_pwm s_pwm4;

/* ==========================================================================
 *  通道操作
 * ========================================================================== */
/**
 * @brief 设置某个支持通道的 PWM 模式、脉宽和输出开关。
 * @param tim TIM3/TIM4；ch 为 1、3 或 4；pulse 为计数单位比较值；enable 非 0 表示输出。
 * @details 用法：内部在通道配置或使能变更时调用，实际可用设备/通道由 n32_pwm_control() 校验。
 *          动作：填入 PWM1/高有效参数，调用对应 OC 初始化接口并启用比较值预装载。
 */
static void pwm_channel_apply(TIM_Module *tim, rt_uint8_t ch,
                              rt_uint16_t pulse, rt_uint8_t enable)
{
    OCInitType oc;

    TIM_InitOcStruct(&oc);
    oc.OcMode      = TIM_OCMODE_PWM1;
    oc.OutputState = enable ? TIM_OUTPUT_STATE_ENABLE : TIM_OUTPUT_STATE_DISABLE;
    oc.Pulse       = pulse;
    oc.OcPolarity  = TIM_OC_POLARITY_HIGH;

    switch (ch)
    {
        case 1:
            TIM_InitOc1(tim, &oc);
            TIM_ConfigOc1Preload(tim, TIM_OC_PRE_LOAD_ENABLE);
            break;
        case 3:
            TIM_InitOc3(tim, &oc);
            TIM_ConfigOc3Preload(tim, TIM_OC_PRE_LOAD_ENABLE);
            break;
        case 4:
            TIM_InitOc4(tim, &oc);
            TIM_ConfigOc4Preload(tim, TIM_OC_PRE_LOAD_ENABLE);
            break;
        default:
            break;
    }
}

/**
 * @brief 更新指定 PWM 通道的比较寄存器。
 * @param tim 定时器；ch 为 1、3 或 4；ccr 为计数单位脉宽。
 * @details 用法：内部更新亮度时调用，参数先由控制回调验证。
 *          动作：选择对应 TIM_SetCmp 接口；其他通道号不执行操作。
 */
static void pwm_set_compare(TIM_Module *tim, rt_uint8_t ch, rt_uint16_t ccr)
{
    switch (ch)
    {
        case 1: TIM_SetCmp1(tim, ccr); break;
        case 3: TIM_SetCmp3(tim, ccr); break;
        case 4: TIM_SetCmp4(tim, ccr); break;
        default: break;
    }
}

/* ==========================================================================
 *  rt_pwm_ops
 * ========================================================================== */
/**
 * @brief 把 RT-Thread PWM 命令转换为定时器周期、占空比和使能操作。
 * @param device pwm3 或 pwm4；cmd 为 PWM_CMD_*；arg 为 rt_pwm_configuration*，period/pulse 单位纳秒。
 * @return RT_EOK=成功；EINVAL=通道/时间无效，EBUSY=同定时器其他活动通道周期冲突，ENOSYS=未知命令，均为负值。
 * @details 用法：应用调用 rt_pwm_set/enable/disable，不直接调用此内部回调。
 *          动作：验证 TIM3 CH3/CH4 或 TIM4 CH1，再将纳秒按 1 us 计数粒度转换，更新 ARR/CCR 或开关输出。
 * @note 同一个定时器的通道共享周期；不是所有 1..4 通道都已接出，非整微秒值会按整数除法截取。
 */
static rt_err_t n32_pwm_control(struct rt_device_pwm *device, int cmd, void *arg)
{
    struct n32_pwm              *pwm = (struct n32_pwm *)device;
    struct rt_pwm_configuration *cfg = (struct rt_pwm_configuration *)arg;
    rt_uint8_t  ch;
    rt_uint32_t arr;
    rt_uint32_t ccr;

    if ((cfg == RT_NULL) || (cfg->channel < 1) || (cfg->channel > PWM_CH_MAX))
    {
        return -RT_EINVAL;
    }
    ch = (rt_uint8_t)cfg->channel;
    if ((pwm->tim == TIM3 && ch != 3 && ch != 4) ||
        (pwm->tim == TIM4 && ch != 1)) return -RT_EINVAL;

    switch (cmd)
    {
        case PWM_CMD_SET:
            /* 纳秒 -> 计数。ARR = 周期 - 1 */
            if (cfg->period < PWM_NS_PER_TICK || cfg->pulse > cfg->period ||
                cfg->period / PWM_NS_PER_TICK > PWM_MAX_ARR)
            {
                return -RT_EINVAL;
            }

            arr = cfg->period / PWM_NS_PER_TICK;
            ccr = cfg->pulse / PWM_NS_PER_TICK;

            for (unsigned c = 1; c <= PWM_CH_MAX; ++c) {
                if (c != ch && pwm->out_en[c] && pwm->arr != arr - 1u) return -RT_EBUSY;
            }
            pwm->arr       = (rt_uint16_t)(arr - 1u);
            pwm->ccr[ch]   = (rt_uint16_t)ccr;

            TIM_SetAutoReload(pwm->tim, pwm->arr);
            pwm_set_compare(pwm->tim, ch, pwm->ccr[ch]);

            /* 通道还没使能的话, 现在只改值不输出; 等 enable 时再一起生效 */
            if (pwm->out_en[ch])
            {
                pwm_channel_apply(pwm->tim, ch, pwm->ccr[ch], 1u);
            }
            return RT_EOK;

        case PWM_CMD_GET:
            cfg->period = (rt_uint32_t)(pwm->arr + 1u) * PWM_NS_PER_TICK;
            cfg->pulse  = (rt_uint32_t)pwm->ccr[ch] * PWM_NS_PER_TICK;
            return RT_EOK;

        case PWM_CMD_ENABLE:
            pwm->out_en[ch] = 1u;
            pwm_channel_apply(pwm->tim, ch, pwm->ccr[ch], 1u);
            return RT_EOK;

        case PWM_CMD_DISABLE:
            pwm->out_en[ch] = 0u;
            pwm_channel_apply(pwm->tim, ch, 0u, 0u);
            return RT_EOK;

        default:
            return -RT_ENOSYS;
    }
}

static const struct rt_pwm_ops s_n32_pwm_ops =
{
    n32_pwm_control,
};

/* ==========================================================================
 *  初始化
 * ========================================================================== */
/**
 * @brief 初始化两组 PWM 输出引脚和 TIM4 重映射。
 * @details 用法：由 drv_pwm_init() 自动调用，实际接线由 board_config.h 指定。
 *          动作：开 GPIO/TIM3/TIM4 时钟，设置复用推挽输出，并应用 TIM4 重映射。
 * @note 更换引脚必须核对通道复用；GP21 使用的 PD13 不在本 PWM 输出集合中。
 */
static void pwm_gpio_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(BSP_GPIO_CLOCKS, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM3 | RCC_APB1_PERIPH_TIM4, ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;      /* 复用推挽 */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* PB0 = TIM3_CH3, PB1 = TIM3_CH4 —— 默认复用, 不需要重映射 */
    gpio.Pin = BSP_PWM3_PINS;
    GPIO_InitPeripheral(BSP_PWM3_PORT, &gpio);

    /* PD12 = TIM4_CH1 —— TIM4 默认在 PB6~PB9, 要开重映射才到 PD12~PD15 */
    GPIO_ConfigPinRemap(BSP_PWM4_REMAP, ENABLE);
    gpio.Pin = BSP_PWM4_PIN;
    GPIO_InitPeripheral(BSP_PWM4_PORT, &gpio);
}

/**
 * @brief 为一个 PWM 设备建立 1 MHz 计数时基，并默认关闭输出。
 * @param pwm 已设置 tim 指针的内部设备对象。
 * @details 用法：drv_pwm_init() 分别对 TIM3 和 TIM4 调用。
 *          动作：按实际 APB 时钟计算预分频，设向上计数，清通道缓存和输出使能，最后启动定时器时基。
 */
static void pwm_timer_init(struct n32_pwm *pwm)
{
    TIM_TimeBaseInitType tb;
    int i;

    RCC_ClocksType clocks;
    RCC_GetClocksFreqValue(&clocks);
    uint32_t timclk = clocks.Pclk1Freq * (clocks.Pclk1Freq == clocks.HclkFreq ? 1u : 2u);
    TIM_InitTimBaseStruct(&tb);
    tb.Prescaler = timclk / 1000000u - 1u;
    tb.CntMode   = TIM_CNT_MODE_UP;
    tb.Period    = PWM_MAX_ARR;             /* 默认最长周期, rt_pwm_set 会改 */
    tb.ClkDiv    = 0x0;
    TIM_InitTimeBase(pwm->tim, &tb);

    pwm->arr = PWM_MAX_ARR;
    for (i = 1; i <= PWM_CH_MAX; i++)
    {
        pwm->ccr[i]    = 0u;
        pwm->out_en[i] = 0u;
    }

    /* 先全部关掉输出, 避免上电瞬间 LED 闪一下 */
    pwm_channel_apply(pwm->tim, 1, 0u, 0u);
    pwm_channel_apply(pwm->tim, 3, 0u, 0u);
    pwm_channel_apply(pwm->tim, 4, 0u, 0u);

    TIM_Enable(pwm->tim, ENABLE);
}

/**
 * @brief 初始化 TIM3/TIM4 并注册 pwm3、pwm4 设备。
 * @return 0=注册完成，否则返回设备注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动调用；应用用 rt_device_find() 找到设备后调用 rt_pwm_set()/enable()。
 *          动作：配置 GPIO，绑定硬件和内部对象，初始化时基，再依次注册两个原生 RT-Thread PWM 设备。
 */
int drv_pwm_init(void)
{
    pwm_gpio_init();

    s_pwm3.tim = TIM3;
    s_pwm4.tim = TIM4;

    pwm_timer_init(&s_pwm3);
    pwm_timer_init(&s_pwm4);

    rt_err_t err = rt_device_pwm_register(&s_pwm3.parent, PWM_DEV_TIM3, &s_n32_pwm_ops, RT_NULL);
    if (err != RT_EOK) return err;
    err = rt_device_pwm_register(&s_pwm4.parent, PWM_DEV_TIM4, &s_n32_pwm_ops, RT_NULL);
    if (err != RT_EOK) return err;

    rt_kprintf("[pwm] %s: TIM3 CH3=PB0 CH4=PB1 | %s: TIM4 CH1=PD12 | "
               "1 tick = %uns\n",
               PWM_DEV_TIM3, PWM_DEV_TIM4, PWM_NS_PER_TICK);
    return 0;
}
INIT_BOARD_EXPORT(drv_pwm_init);
