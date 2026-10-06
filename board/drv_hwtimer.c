/* TIM6 is reserved for hwtimer; TIM3/4 remain owned by PWM. */
#include <rtthread.h>
#include <rtdevice.h>
#include "n32g45x.h"

static rt_hwtimer_t timer6;
static struct rt_hwtimer_info info;
/**
 * @brief 查询 TIM6 的实际输入时钟。
 * @return 时钟频率，单位 Hz。
 * @details 用法：内部计算预分频和设备频率范围时调用。
 *          动作：读 RCC 的 PCLK1/HCLK；APB1 无分频时取 PCLK1，否则按定时器倍频规则取两倍。
 */
static rt_uint32_t timer_clock(void)
{
    RCC_ClocksType clocks;
    RCC_GetClocksFreqValue(&clocks);
    return clocks.Pclk1Freq * (clocks.Pclk1Freq == clocks.HclkFreq ? 1u : 2u);
}
/**
 * @brief 停止 TIM6 并清除外设和 NVIC 待处理中断。
 * @param timer 定时器框架对象，本驱动固定使用 TIM6。
 * @details 用法：由 hwtimer 框架停止回调或本文件配置流程调用。
 *          动作：关计数器、关 UPDATE 中断，再清两层挂起标志，防止重启后收到旧到期事件。
 */
static void stop(rt_hwtimer_t *timer)
{
    (void)timer;
    TIM_Enable(TIM6, DISABLE);
    TIM_ConfigInt(TIM6, TIM_INT_UPDATE, DISABLE);
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
    NVIC_ClearPendingIRQ(TIM6_IRQn);
}
/**
 * @brief 初始化或停用 TIM6 硬件计数器。
 * @param timer 已配置 freq 的框架对象；state 非 0 初始化，0 停用。
 * @details 用法：由设备 open/close 等框架路径调用，不作为应用初始化入口。
 *          动作：开时钟并停止旧计数；启用时设置预分频/向上计数/NVIC，停用时关闭 NVIC。
 */
static void init(rt_hwtimer_t *timer, rt_uint32_t state)
{
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE);
    stop(timer);
    if (state) {
        TIM_TimeBaseInitType base;
        TIM_InitTimBaseStruct(&base);
        base.Prescaler = timer_clock() / timer->freq - 1u;
        base.Period = 65535u;
        base.CntMode = TIM_CNT_MODE_UP;
        TIM_InitTimeBase(TIM6, &base);
        TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
        NVIC_SetPriority(TIM6_IRQn, 3);
        NVIC_EnableIRQ(TIM6_IRQn);
    } else {
        NVIC_DisableIRQ(TIM6_IRQn);
    }
}
/**
 * @brief 按指定计数值开始一轮 TIM6 计时。
 * @param timer 框架对象；count 为 1..65535；mode 由框架解释，本回调不直接处理单次/周期差异。
 * @return RT_EOK=开始成功，-RT_EINVAL=计数范围无效。
 * @details 用法：应用向 hwtimer 写入时间值，由框架换算 count 后调用。
 *          动作：停旧计数，设置 ARR=count-1 并清 CNT，更新寄存器后清挂起标志，再开中断和计数。
 * @note 单次停止和超过硬件计数范围的分段计时由 rt_device_hwtimer_isr() 协助处理。
 */
static rt_err_t start(rt_hwtimer_t *timer, rt_uint32_t count, rt_hwtimer_mode_t mode)
{
    (void)mode; /* Framework handles one-shot and extended-period cycles in ISR. */
    if (!count || count > 65535u) return -RT_EINVAL;
    stop(timer);
    TIM_SetAutoReload(TIM6, count - 1u);
    TIM_SetCnt(TIM6, 0);
    TIM_GenerateEvent(TIM6, TIM_EVT_SRC_UPDATE);
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
    NVIC_ClearPendingIRQ(TIM6_IRQn);
    TIM_ConfigInt(TIM6, TIM_INT_UPDATE, ENABLE);
    TIM_Enable(TIM6, ENABLE);
    return RT_EOK;
}
/**
 * @brief 读取 TIM6 当前计数寄存器。
 * @param timer 框架对象，当前实现固定访问 TIM6。
 * @return 当前向上计数值，不是微秒值。
 * @details 用法：由 hwtimer 框架读取剩余/经过时间时使用。
 *          动作：直接读取 CNT，不停止计数器。
 */
static rt_uint32_t count_get(rt_hwtimer_t *timer)
{
    (void)timer;
    return TIM_GetCnt(TIM6);
}
/**
 * @brief 处理 TIM6 计数频率设置。
 * @param timer 框架对象；cmd 仅支持 HWTIMER_CTRL_FREQ_SET；arg 为 rt_uint32_t Hz 地址。
 * @return RT_EOK=完成，-RT_EINVAL=频率/参数无效，-RT_ENOSYS=不支持的命令。
 * @details 用法：应用通过 rt_device_control() 设置频率，需能整除定时器输入时钟且预分频在硬件范围内。
 *          动作：验证频率，停止计数器并立即更新预分频，清 UPDATE 标志；随后需重新写入计时时间启动。
 */
static rt_err_t control(rt_hwtimer_t *timer, rt_uint32_t cmd, void *arg)
{
    if (cmd != HWTIMER_CTRL_FREQ_SET) return -RT_ENOSYS;
    if (!arg) return -RT_EINVAL;
    rt_uint32_t hz = *(rt_uint32_t *)arg, clk = timer_clock();
    if (!hz || clk % hz || clk / hz > 65536u) return -RT_EINVAL;
    stop(timer);
    TIM_ConfigPrescaler(TIM6, clk / hz - 1u, TIM_PSC_RELOAD_MODE_IMMEDIATE);
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
    return RT_EOK;
}
static const struct rt_hwtimer_ops ops = {init, start, stop, count_get, control};
/**
 * @brief 注册独立于 PWM 的 timer6 硬件定时器设备。
 * @return RT_EOK 或设备注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动执行一次；应用按 rt_device_find("timer6")、open、control、write 使用。
 *          动作：根据时钟填写频率范围和 16 位计数能力，绑定回调并注册到 hwtimer 框架。
 */
static int register_timer(void)
{
    info.maxfreq = timer_clock();
    info.minfreq = (timer_clock() + 65535u) / 65536u;
    info.maxcnt = 65535u;
    info.cntmode = HWTIMER_CNTMODE_UP;
    timer6.ops = &ops;
    timer6.info = &info;
    return rt_device_hwtimer_register(&timer6, "timer6", RT_NULL);
}
INIT_BOARD_EXPORT(register_timer);
/**
 * @brief 处理 TIM6 UPDATE 中断并交给 RT-Thread hwtimer 框架。
 * @details 用法：硬件自动进入，应用通过 rx_indicate 接收完成通知。
 *          动作：标记中断进入，确认并清 UPDATE，再调用 rt_device_hwtimer_isr()，最后退出中断。
 * @note 用户到期回调也在中断上下文执行，应使用信号量通知线程，不能进行延时或阻塞 I/O。
 */
void TIM6_IRQHandler(void)
{
    rt_interrupt_enter();
    if (TIM_GetIntStatus(TIM6, TIM_INT_UPDATE) != RESET) {
        TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
        rt_device_hwtimer_isr(&timer6);
    }
    rt_interrupt_leave();
}
