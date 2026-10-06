/**
 * @file    drv_gpio.c
 * @brief   RT-Thread PIN 设备驱动 (N32G452)
 *
 *  注册后应用层就能用 rt_pin_mode / rt_pin_write / rt_pin_read 操作任意 IO,
 *  不需要直接碰寄存器, 这是"应用层不碰寄存器"分层规则的一部分。
 *
 *  引脚编号约定 (与 RT-Thread 官方 BSP 一致):
 *      pin = 端口序号 * 16 + 位序号
 *      PA0..PA15 -> 0..15     PB0..PB15 -> 16..31    PC0..PC15 -> 32..47
 *      PD0..PD15 -> 48..63    PE0..PE15 -> 64..79    PF0..PF15 -> 80..95
 *      PG0..PG15 -> 96..111
 *  例如 PC5 = 37, PE0 = 64, PD5 = 53。
 *
 *  本文件只做 GPIO。EXTI 中断的入口函数放在文件末尾 (老版本 RT-Thread 的
 *  pin 框架把 ISR 也交给 BSP 提供)。
 *
 *  参考: SDK projects/n32g45x_EVAL/examples/RT_Thread/DeviceDrivers/gpio/src/drv_gpio.c
 */

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>

#include "n32g45x.h"
#include "n32g45x_gpio.h"
#include "n32g45x_rcc.h"
#include "n32g45x_exti.h"

/* ==========================================================================
 *  端口 / 引脚映射表
 * ========================================================================== */
struct n32_pin_index
{
    GPIO_Module *gpio;
    rt_uint16_t  pin;       /* GPIO_PIN_x */
    rt_int8_t    index;     /* 位序号 0..15, -1 表示不存在 */
};

#define N32_PIN(n, gpio, pinmask) { gpio, pinmask, n }

static const struct n32_pin_index s_pins[] =
{
    /* PA */
    N32_PIN( 0, GPIOA, GPIO_PIN_0 ),  N32_PIN( 1, GPIOA, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOA, GPIO_PIN_2 ),  N32_PIN( 3, GPIOA, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOA, GPIO_PIN_4 ),  N32_PIN( 5, GPIOA, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOA, GPIO_PIN_6 ),  N32_PIN( 7, GPIOA, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOA, GPIO_PIN_8 ),  N32_PIN( 9, GPIOA, GPIO_PIN_9 ),
    N32_PIN(10, GPIOA, GPIO_PIN_10),  N32_PIN(11, GPIOA, GPIO_PIN_11),
    N32_PIN(12, GPIOA, GPIO_PIN_12),  N32_PIN(13, GPIOA, GPIO_PIN_13),
    N32_PIN(14, GPIOA, GPIO_PIN_14),  N32_PIN(15, GPIOA, GPIO_PIN_15),
    /* PB */
    N32_PIN( 0, GPIOB, GPIO_PIN_0 ),  N32_PIN( 1, GPIOB, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOB, GPIO_PIN_2 ),  N32_PIN( 3, GPIOB, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOB, GPIO_PIN_4 ),  N32_PIN( 5, GPIOB, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOB, GPIO_PIN_6 ),  N32_PIN( 7, GPIOB, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOB, GPIO_PIN_8 ),  N32_PIN( 9, GPIOB, GPIO_PIN_9 ),
    N32_PIN(10, GPIOB, GPIO_PIN_10),  N32_PIN(11, GPIOB, GPIO_PIN_11),
    N32_PIN(12, GPIOB, GPIO_PIN_12),  N32_PIN(13, GPIOB, GPIO_PIN_13),
    N32_PIN(14, GPIOB, GPIO_PIN_14),  N32_PIN(15, GPIOB, GPIO_PIN_15),
    /* PC */
    N32_PIN( 0, GPIOC, GPIO_PIN_0 ),  N32_PIN( 1, GPIOC, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOC, GPIO_PIN_2 ),  N32_PIN( 3, GPIOC, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOC, GPIO_PIN_4 ),  N32_PIN( 5, GPIOC, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOC, GPIO_PIN_6 ),  N32_PIN( 7, GPIOC, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOC, GPIO_PIN_8 ),  N32_PIN( 9, GPIOC, GPIO_PIN_9 ),
    N32_PIN(10, GPIOC, GPIO_PIN_10),  N32_PIN(11, GPIOC, GPIO_PIN_11),
    N32_PIN(12, GPIOC, GPIO_PIN_12),  N32_PIN(13, GPIOC, GPIO_PIN_13),
    N32_PIN(14, GPIOC, GPIO_PIN_14),  N32_PIN(15, GPIOC, GPIO_PIN_15),
    /* PD */
    N32_PIN( 0, GPIOD, GPIO_PIN_0 ),  N32_PIN( 1, GPIOD, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOD, GPIO_PIN_2 ),  N32_PIN( 3, GPIOD, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOD, GPIO_PIN_4 ),  N32_PIN( 5, GPIOD, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOD, GPIO_PIN_6 ),  N32_PIN( 7, GPIOD, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOD, GPIO_PIN_8 ),  N32_PIN( 9, GPIOD, GPIO_PIN_9 ),
    N32_PIN(10, GPIOD, GPIO_PIN_10),  N32_PIN(11, GPIOD, GPIO_PIN_11),
    N32_PIN(12, GPIOD, GPIO_PIN_12),  N32_PIN(13, GPIOD, GPIO_PIN_13),
    N32_PIN(14, GPIOD, GPIO_PIN_14),  N32_PIN(15, GPIOD, GPIO_PIN_15),
    /* PE */
    N32_PIN( 0, GPIOE, GPIO_PIN_0 ),  N32_PIN( 1, GPIOE, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOE, GPIO_PIN_2 ),  N32_PIN( 3, GPIOE, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOE, GPIO_PIN_4 ),  N32_PIN( 5, GPIOE, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOE, GPIO_PIN_6 ),  N32_PIN( 7, GPIOE, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOE, GPIO_PIN_8 ),  N32_PIN( 9, GPIOE, GPIO_PIN_9 ),
    N32_PIN(10, GPIOE, GPIO_PIN_10),  N32_PIN(11, GPIOE, GPIO_PIN_11),
    N32_PIN(12, GPIOE, GPIO_PIN_12),  N32_PIN(13, GPIOE, GPIO_PIN_13),
    N32_PIN(14, GPIOE, GPIO_PIN_14),  N32_PIN(15, GPIOE, GPIO_PIN_15),
    /* PF */
    N32_PIN( 0, GPIOF, GPIO_PIN_0 ),  N32_PIN( 1, GPIOF, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOF, GPIO_PIN_2 ),  N32_PIN( 3, GPIOF, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOF, GPIO_PIN_4 ),  N32_PIN( 5, GPIOF, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOF, GPIO_PIN_6 ),  N32_PIN( 7, GPIOF, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOF, GPIO_PIN_8 ),  N32_PIN( 9, GPIOF, GPIO_PIN_9 ),
    N32_PIN(10, GPIOF, GPIO_PIN_10),  N32_PIN(11, GPIOF, GPIO_PIN_11),
    N32_PIN(12, GPIOF, GPIO_PIN_12),  N32_PIN(13, GPIOF, GPIO_PIN_13 ),
    N32_PIN(14, GPIOF, GPIO_PIN_14),  N32_PIN(15, GPIOF, GPIO_PIN_15),
    /* PG */
    N32_PIN( 0, GPIOG, GPIO_PIN_0 ),  N32_PIN( 1, GPIOG, GPIO_PIN_1 ),
    N32_PIN( 2, GPIOG, GPIO_PIN_2 ),  N32_PIN( 3, GPIOG, GPIO_PIN_3 ),
    N32_PIN( 4, GPIOG, GPIO_PIN_4 ),  N32_PIN( 5, GPIOG, GPIO_PIN_5 ),
    N32_PIN( 6, GPIOG, GPIO_PIN_6 ),  N32_PIN( 7, GPIOG, GPIO_PIN_7 ),
    N32_PIN( 8, GPIOG, GPIO_PIN_8 ),  N32_PIN( 9, GPIOG, GPIO_PIN_9 ),
    N32_PIN(10, GPIOG, GPIO_PIN_10),  N32_PIN(11, GPIOG, GPIO_PIN_11),
    N32_PIN(12, GPIOG, GPIO_PIN_12),  N32_PIN(13, GPIOG, GPIO_PIN_13),
    N32_PIN(14, GPIOG, GPIO_PIN_14),  N32_PIN(15, GPIOG, GPIO_PIN_15),
};

#define PIN_TABLE_NUM   (sizeof(s_pins) / sizeof(s_pins[0]))

/**
 * @brief 将 RT-Thread 连续引脚编号转换为端口和位掩码。
 * @param pin 端口序号×16+位号，例如 PC5=37。
 * @return 对应静态映射项；编号超出表范围返回 RT_NULL。
 * @details 用法：PIN 回调内部查表；表内编号存在不代表该封装实际引出该脚。
 *          动作：检查范围后返回 s_pins 中的条目，不配置硬件。
 */
static const struct n32_pin_index *n32_get_pin(rt_base_t pin)
{
    if ((pin < 0) || (pin >= (rt_base_t)PIN_TABLE_NUM))
    {
        return RT_NULL;
    }
    return &s_pins[pin];
}

/* ==========================================================================
 *  基本操作
 * ========================================================================== */
/**
 * @brief 设置引脚的输入、输出、上拉、下拉或开漏模式。
 * @param device PIN 设备；pin 为连续编号；mode 为 PIN_MODE_*。
 * @details 用法：应用调用 rt_pin_mode(pin, mode)，由框架转到本回调。
 *          动作：查映射，把 RT 模式转换为 N32 GPIO 模式并配置硬件；未知引脚/模式直接忽略。
 */
static void n32_pin_mode(struct rt_device *device, rt_base_t pin, rt_base_t mode)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);
    GPIO_InitType gpio;

    (void)device;

    if (idx == RT_NULL)
    {
        return;
    }

    GPIO_InitStruct(&gpio);
    gpio.Pin        = idx->pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    switch (mode)
    {
        case PIN_MODE_OUTPUT:
            gpio.GPIO_Mode = GPIO_Mode_Out_PP;
            break;
        case PIN_MODE_INPUT:
            gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            break;
        case PIN_MODE_INPUT_PULLUP:
            gpio.GPIO_Mode = GPIO_Mode_IPU;
            break;
        case PIN_MODE_INPUT_PULLDOWN:
            gpio.GPIO_Mode = GPIO_Mode_IPD;
            break;
        case PIN_MODE_OUTPUT_OD:
            gpio.GPIO_Mode = GPIO_Mode_Out_OD;
            break;
        default:
            return;
    }

    GPIO_InitPeripheral(idx->gpio, &gpio);
}

/**
 * @brief 修改一个已配置输出引脚的电平。
 * @param device PIN 设备；pin 为连续编号；value 为 0=低、非 0=高。
 * @details 用法：应用调用 rt_pin_write()，通常先用 rt_pin_mode() 设置输出模式。
 *          动作：查端口/位号后调用 GPIO_WriteBit()；非法引脚不执行写入。
 */
static void n32_pin_write(struct rt_device *device, rt_base_t pin, rt_base_t value)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);

    (void)device;

    if (idx == RT_NULL)
    {
        return;
    }
    GPIO_WriteBit(idx->gpio, idx->pin, (Bit_OperateType)(value ? 1 : 0));
}

/**
 * @brief 读取指定引脚的实际输入电平。
 * @param device PIN 设备；pin 为连续编号。
 * @return PIN_HIGH/PIN_LOW；引脚编号无效也返回 PIN_LOW。
 * @details 用法：应用调用 rt_pin_read()，先配置合适的输入和上下拉。
 *          动作：查表后读取 GPIO 输入寄存器，不读取或改变输出缓存。
 */
static int n32_pin_read(struct rt_device *device, rt_base_t pin)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);

    (void)device;

    if (idx == RT_NULL)
    {
        return PIN_LOW;
    }
    return (GPIO_ReadInputDataBit(idx->gpio, idx->pin) != 0) ? PIN_HIGH : PIN_LOW;
}

/* ==========================================================================
 *  中断支持 (EXTI)
 *   同一时刻每个"位序号"只能有一个引脚占用 EXTI, 这是 N32/STM32 的硬件限制,
 *   所以表格按 16 个位序号组织, 与端口无关。
 * ========================================================================== */
struct n32_pin_irq_map
{
    rt_uint32_t pinbit;
    IRQn_Type   irqno;
};

static const struct n32_pin_irq_map s_pin_irq_map[16] =
{
    { GPIO_PIN_0,  EXTI0_IRQn     }, { GPIO_PIN_1,  EXTI1_IRQn     },
    { GPIO_PIN_2,  EXTI2_IRQn     }, { GPIO_PIN_3,  EXTI3_IRQn     },
    { GPIO_PIN_4,  EXTI4_IRQn     }, { GPIO_PIN_5,  EXTI9_5_IRQn   },
    { GPIO_PIN_6,  EXTI9_5_IRQn   }, { GPIO_PIN_7,  EXTI9_5_IRQn   },
    { GPIO_PIN_8,  EXTI9_5_IRQn   }, { GPIO_PIN_9,  EXTI9_5_IRQn   },
    { GPIO_PIN_10, EXTI15_10_IRQn }, { GPIO_PIN_11, EXTI15_10_IRQn },
    { GPIO_PIN_12, EXTI15_10_IRQn }, { GPIO_PIN_13, EXTI15_10_IRQn },
    { GPIO_PIN_14, EXTI15_10_IRQn }, { GPIO_PIN_15, EXTI15_10_IRQn },
};

static struct rt_pin_irq_hdr s_irq_hdr[16] =
{
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
    { -1, 0, RT_NULL, RT_NULL }, { -1, 0, RT_NULL, RT_NULL },
};

static rt_uint32_t s_irq_enable_mask = 0;

/**
 * @brief 把单引脚位掩码转换成 0..15 位序号。
 * @param bit 仅允许一个低 16 位比特置位。
 * @return 匹配的位序号；不符合单比特格式时返回 -1。
 * @details 用法：EXTI 管理内部使用。
 *          动作：依次比较 1<<i 与输入掩码，找到匹配项即返回。
 */
static rt_int32_t n32_bit2bitno(rt_uint32_t bit)
{
    rt_int32_t i;
    for (i = 0; i < 16; i++)
    {
        if ((1u << i) == bit)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 把 GPIO 外设地址转换为 EXTI 端口选择码。
 * @param gpio 来自有效引脚表的 GPIOA..GPIOG 地址。
 * @return 相应端口源编号；未知地址退回 GPIOA，调用方应保证来源有效。
 * @details 用法：启用引脚中断时与位序号一起传给 GPIO_ConfigEXTILine()。
 *          动作：按外设地址选择端口常量，不直接写寄存器。
 */
static rt_int32_t n32_port2source(GPIO_Module *gpio)
{
    if (gpio == GPIOA) return GPIOA_PORT_SOURCE;
    if (gpio == GPIOB) return GPIOB_PORT_SOURCE;
    if (gpio == GPIOC) return GPIOC_PORT_SOURCE;
    if (gpio == GPIOD) return GPIOD_PORT_SOURCE;
    if (gpio == GPIOE) return GPIOE_PORT_SOURCE;
    if (gpio == GPIOF) return GPIOF_PORT_SOURCE;
    if (gpio == GPIOG) return GPIOG_PORT_SOURCE;
    return GPIOA_PORT_SOURCE;
}

static rt_err_t n32_pin_irq_enable(struct rt_device *, rt_base_t, rt_uint32_t);

/**
 * @brief 为引脚预留 EXTI 线并记录回调，尚不打开中断。
 * @param device PIN 设备；pin 为连续编号；mode 为上升/下降/双边沿；hdr 为回调；args 为回调参数。
 * @return RT_EOK=绑定成功或相同绑定已存在；冲突返回 -RT_EBUSY，非法参数返回其他负值。
 * @details 用法：应用调用 rt_pin_attach_irq() 后，再调用 rt_pin_irq_enable()。
 *          动作：验证映射和模式，在短临界区内保存 pin、mode、hdr、args。
 * @note 同位号的不同端口共享 EXTI 线，例如 PA13 与 PD13 不能同时占用 EXTI13；回调在中断中执行。
 */
static rt_err_t n32_pin_attach_irq(struct rt_device *device, rt_int32_t pin,
                                   rt_uint32_t mode,
                                   void (*hdr)(void *args), void *args)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);
    rt_int32_t bitno;
    rt_base_t level;

    (void)device;

    if (idx == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    bitno = n32_bit2bitno(idx->pin);
    if (bitno < 0)
    {
        return -RT_ENOSYS;
    }

    if (!hdr || (mode != PIN_IRQ_MODE_RISING && mode != PIN_IRQ_MODE_FALLING &&
        mode != PIN_IRQ_MODE_RISING_FALLING)) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();

    if ((s_irq_hdr[bitno].pin == pin) &&
        (s_irq_hdr[bitno].hdr == hdr) &&
        (s_irq_hdr[bitno].mode == mode) &&
        (s_irq_hdr[bitno].args == args))
    {
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }

    if (s_irq_hdr[bitno].pin != -1)
    {
        rt_hw_interrupt_enable(level);
        return -RT_EBUSY;         /* 该 EXTI 线已被别的引脚占用 */
    }

    s_irq_hdr[bitno].pin  = pin;
    s_irq_hdr[bitno].hdr  = hdr;
    s_irq_hdr[bitno].mode = mode;
    s_irq_hdr[bitno].args = args;

    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

/**
 * @brief 关闭并解除指定引脚的 EXTI 回调绑定。
 * @param device PIN 设备；pin 为绑定时使用的连续编号。
 * @return RT_EOK=解除成功，未绑定到该引脚或映射无效时返回负错误码。
 * @details 用法：应用调用 rt_pin_detach_irq()，解除后可让其他端口的同位号引脚使用该 EXTI。
 *          动作：先禁用中断，再在临界区清空回调表条目。
 */
static rt_err_t n32_pin_detach_irq(struct rt_device *device, rt_int32_t pin)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);
    rt_int32_t bitno;
    rt_base_t level;

    (void)device;

    if (idx == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    bitno = n32_bit2bitno(idx->pin);
    if (bitno < 0)
    {
        return -RT_ENOSYS;
    }

    if (s_irq_hdr[bitno].pin != pin) return -RT_EINVAL;
    n32_pin_irq_enable(device, pin, PIN_IRQ_DISABLE);
    level = rt_hw_interrupt_disable();
    s_irq_hdr[bitno].pin  = -1;
    s_irq_hdr[bitno].hdr  = RT_NULL;
    s_irq_hdr[bitno].mode = 0;
    s_irq_hdr[bitno].args = RT_NULL;
    rt_hw_interrupt_enable(level);

    return RT_EOK;
}

/**
 * @brief 设置一条 EXTI 对应 NVIC 向量的使能状态。
 * @param irqno EXTI 向量号；cmd 为 ENABLE 或 DISABLE。
 * @details 用法：由引脚中断使能逻辑调用；共享向量须确认所有成员都关闭后才禁用。
 *          动作：填写 NVIC 初始化结构和优先级字段，再调用芯片库配置。
 */
static void n32_irq_nvic_cfg(IRQn_Type irqno, FunctionalState cmd)
{
    NVIC_InitType nvic;

    nvic.NVIC_IRQChannel                   = irqno;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = cmd;
    NVIC_Init(&nvic);
}

/**
 * @brief 打开或关闭已经绑定的引脚 EXTI 中断。
 * @param device PIN 设备；pin 为连续编号；enabled 为 PIN_IRQ_ENABLE 或 PIN_IRQ_DISABLE。
 * @return RT_EOK=成功，未绑定/参数无效时返回负错误码。
 * @details 用法：rt_pin_attach_irq() 成功后调用 rt_pin_irq_enable()。
 *          动作：启用时映射端口、配置边沿、清挂起标志并开 NVIC；禁用时清本线使能位及挂起状态。
 * @note EXTI5..9 和 EXTI10..15 共用向量，仅在同组全部关闭时禁用其 NVIC，避免影响其他设备。
 */
static rt_err_t n32_pin_irq_enable(struct rt_device *device, rt_base_t pin,
                                   rt_uint32_t enabled)
{
    const struct n32_pin_index *idx = n32_get_pin(pin);
    rt_int32_t bitno;
    rt_base_t level;
    EXTI_InitType exti;

    (void)device;

    if (idx == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    bitno = n32_bit2bitno(idx->pin);
    if (bitno < 0)
    {
        return -RT_ENOSYS;
    }

    if (s_irq_hdr[bitno].pin != pin) return -RT_EINVAL;
    if (enabled == PIN_IRQ_ENABLE)
    {
        level = rt_hw_interrupt_disable();

        if (s_irq_hdr[bitno].pin == -1)
        {
            rt_hw_interrupt_enable(level);
            return -RT_ENOSYS;      /* 必须先 attach */
        }

        EXTI_InitStruct(&exti);
        switch (s_irq_hdr[bitno].mode)
        {
            case PIN_IRQ_MODE_RISING:
                exti.EXTI_Trigger = EXTI_Trigger_Rising;
                break;
            case PIN_IRQ_MODE_FALLING:
                exti.EXTI_Trigger = EXTI_Trigger_Falling;
                break;
            case PIN_IRQ_MODE_RISING_FALLING:
                exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
                break;
            default:
                rt_hw_interrupt_enable(level);
                return -RT_EINVAL;
        }

        /* 把 EXTI 线接到对应端口 */
        RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
        GPIO_ConfigEXTILine((uint8_t)n32_port2source(idx->gpio), (uint8_t)bitno);

        exti.EXTI_Line    = idx->pin;
        exti.EXTI_Mode    = EXTI_Mode_Interrupt;
        exti.EXTI_LineCmd = ENABLE;
        EXTI_InitPeripheral(&exti);

        EXTI_ClrITPendBit(idx->pin);
        n32_irq_nvic_cfg(s_pin_irq_map[bitno].irqno, ENABLE);
        s_irq_enable_mask |= s_pin_irq_map[bitno].pinbit;

        rt_hw_interrupt_enable(level);
    }
    else if (enabled == PIN_IRQ_DISABLE)
    {
        level = rt_hw_interrupt_disable();

        EXTI_InitStruct(&exti);
        exti.EXTI_Line = idx->pin;
        exti.EXTI_Mode = EXTI_Mode_Interrupt;
        exti.EXTI_LineCmd = DISABLE;
        EXTI_InitPeripheral(&exti);
        EXTI_ClrITPendBit(idx->pin);
        s_irq_enable_mask &= ~s_pin_irq_map[bitno].pinbit;

        /* EXTI9_5 / EXTI15_10 是多引脚共用一条中断线, 要等这条线上
         * 所有引脚都关掉了才能关 NVIC */
        if ((bitno >= 5) && (bitno <= 9))
        {
            if ((s_irq_enable_mask &
                 (GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9)) == 0)
            {
                n32_irq_nvic_cfg(EXTI9_5_IRQn, DISABLE);
            }
        }
        else if (bitno >= 10)
        {
            if ((s_irq_enable_mask &
                 (GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                  GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)) == 0)
            {
                n32_irq_nvic_cfg(EXTI15_10_IRQn, DISABLE);
            }
        }
        else
        {
            n32_irq_nvic_cfg(s_pin_irq_map[bitno].irqno, DISABLE);
        }

        rt_hw_interrupt_enable(level);
    }
    else
    {
        return -RT_ENOSYS;
    }

    return RT_EOK;
}

static const struct rt_pin_ops s_n32_pin_ops =
{
    n32_pin_mode,
    n32_pin_write,
    n32_pin_read,
    n32_pin_attach_irq,
    n32_pin_detach_irq,
    n32_pin_irq_enable,
};

/**
 * @brief 启用 GPIO 时钟并注册 RT-Thread pin 设备。
 * @return rt_device_pin_register() 的结果。
 * @details 用法：INIT_BOARD_EXPORT 自动调用一次，此后应用可使用 rt_pin_* 接口。
 *          动作：打开端口与 AFIO 时钟，注册模式、读写和中断操作表；不会预先指定每个引脚的用途。
 */
int rt_hw_pin_init(void)
{
    /* 打开所有 GPIO 端口时钟 (挂 APB2) */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB |
                            RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_GPIOD |
                            RCC_APB2_PERIPH_GPIOE | RCC_APB2_PERIPH_GPIOF |
                            RCC_APB2_PERIPH_GPIOG | RCC_APB2_PERIPH_AFIO, ENABLE);

    return rt_device_pin_register("pin", &s_n32_pin_ops, RT_NULL);
}
INIT_BOARD_EXPORT(rt_hw_pin_init);

/* ==========================================================================
 *  EXTI 中断入口
 *   裸机版的 src/n32g45x_it.c 在 RT-Thread 预设里不参与编译,
 *   所以这里给出这些向量, 不会重复定义。
 * ========================================================================== */
/**
 * @brief 调用某一 EXTI 位号已绑定的用户回调。
 * @param bitno 0..15 的有效 EXTI 线号。
 * @details 用法：由 n32_exti_handler() 在硬件中断内调用，应用不要直接调用。
 *          动作：回调非空时传入注册的 args；用户处理必须遵守中断上下文约束。
 */
static void n32_pin_irq_dispatch(rt_int32_t bitno)
{
    if (s_irq_hdr[bitno].hdr != RT_NULL)
    {
        s_irq_hdr[bitno].hdr(s_irq_hdr[bitno].args);
    }
}

/**
 * @brief 检查并处理一条已启用 EXTI 线的挂起事件。
 * @param bitno 0..15 的有效线号。
 * @details 用法：由单独或共享 EXTI 向量入口调用。
 *          动作：同时检查软件使能掩码和硬件挂起位，先清标志，再派发回调，避免同一挂起重复处理。
 */
static void n32_exti_handler(rt_int32_t bitno)
{
    rt_uint32_t line = 1u << bitno;

    if ((s_irq_enable_mask & line) && EXTI_GetStatusFlag(line) != RESET)
    {
        EXTI_ClrITPendBit(line);
        n32_pin_irq_dispatch(bitno);
    }
}

/**
 * @brief EXTI0 硬件中断入口。
 * @details 用法：硬件自动调用，应用通过 rt_pin_attach_irq() 注册处理。
 *          动作：进入 RT 中断上下文、检查并派发 0 号线、退出中断并允许调度。
 */
void EXTI0_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(0);
    rt_interrupt_leave();
}

/**
 * @brief EXTI1 硬件中断入口。
 * @details 用法：硬件自动调用，应用通过 PIN 框架绑定回调。
 *          动作：在 rt_interrupt_enter/leave 之间处理 1 号线。
 */
void EXTI1_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(1);
    rt_interrupt_leave();
}

/**
 * @brief EXTI2 硬件中断入口。
 * @details 用法：硬件自动调用，应用不要再定义同名向量函数。
 *          动作：在 RT 中断进入/退出保护内处理 2 号线。
 */
void EXTI2_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(2);
    rt_interrupt_leave();
}

/**
 * @brief EXTI3 硬件中断入口。
 * @details 用法：硬件自动调用，业务回调通过 PIN 设备注册。
 *          动作：检查并处理 3 号线，保持内核中断嵌套计数正确。
 */
void EXTI3_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(3);
    rt_interrupt_leave();
}

/**
 * @brief EXTI4 硬件中断入口。
 * @details 用法：由向量表调用，不作为普通函数使用。
 *          动作：通知 RT 进入中断、处理 4 号线，再通知退出。
 */
void EXTI4_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(4);
    rt_interrupt_leave();
}

/**
 * @brief EXTI5..9 共用的硬件中断入口。
 * @details 用法：由硬件自动调用，各引脚分别通过 PIN 框架绑定。
 *          动作：一次进入 RT 中断上下文，逐条检查 5..9 号线，只派发已使能且挂起的事件。
 */
void EXTI9_5_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(5);
    n32_exti_handler(6);
    n32_exti_handler(7);
    n32_exti_handler(8);
    n32_exti_handler(9);
    rt_interrupt_leave();
}

/**
 * @brief EXTI10..15 共用的硬件中断入口，包含默认 GP21 INTN 的 EXTI13。
 * @details 用法：由硬件自动调用，GP21 回调由 PIN 框架绑定到 PD13。
 *          动作：进入 RT 中断上下文，逐条检查 10..15 号线，处理完成后统一退出。
 */
void EXTI15_10_IRQHandler(void)
{
    rt_interrupt_enter();
    n32_exti_handler(10);
    n32_exti_handler(11);
    n32_exti_handler(12);
    n32_exti_handler(13);
    n32_exti_handler(14);
    n32_exti_handler(15);
    rt_interrupt_leave();
}
