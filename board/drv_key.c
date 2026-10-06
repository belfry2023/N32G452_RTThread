/**
 * @file    drv_key.c
 * @brief   按键驱动实现 —— 3 按键, 消抖 + 手势识别
 *
 *  数据流:
 *      定时器每 10ms 扫描一次
 *        -> rt_pin_read() 读电平 (走 RT-Thread PIN 设备, 不碰寄存器)
 *        -> 连续 3 次相同才认 (30ms 消抖)
 *        -> 喂给 key_gesture.c 的状态机
 *        -> 手势进队列, 上层用 key_take_gesture() 取
 *
 *  ⚠️ 改按键引脚只改下面的宏。见 inc/board_config.h
 */

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>

#include "drv_key.h"
#include "board_config.h"

/* ==========================================================================
 *  按键引脚   ← 改按键只改这里
 *
 *  RT-Thread 的引脚编号 = 端口序号 * 16 + 位序号:
 *      PA=0..15  PB=16..31  PC=32..47  PD=48..63  PE=64..79  PF=80..95  PG=96..111
 *  所以 PC5 = 2*16+5 = 37。
 *
 *  选 PC5/PC6/PC7 是因为: SPI3 占 PA0/PA1/PC2/PC3/PC4, LCD 占 GPIOE 全部 + PD5,
 *  这三根是空着的。
 * ========================================================================== */
#define KEY_PREV_PIN BSP_KEY_PREV_PIN
#define KEY_NEXT_PIN BSP_KEY_NEXT_PIN
#define KEY_ENTER_PIN BSP_KEY_ENTER_PIN

#define KEY_PREV_NAME       "PC5"
#define KEY_NEXT_NAME       "PC6"
#define KEY_ENTER_NAME      "PC7"

/* ==========================================================================
 *  时间参数   ← 手感不对就调这几个
 * ========================================================================== */
#define KEY_SCAN_MS         10      /* 扫描周期 */
#define KEY_DEBOUNCE_CNT    3       /* 连续 N 次相同才认 -> 30ms 消抖 */
#define KEY_LONG_MS         600     /* 长按阈值: 按住超过它算长按 */
#define KEY_SEQ_MS          300     /* 组合手势窗口: 松手后等这么久看有没有第二次按下 */

/* ⚠️ 组合手势的固有代价:
 *    第一个手势必须等满 KEY_SEQ_MS 才能确定 —— 因为"短按"和"两次短按"的前半段
 *    完全一样。所以 ENTER 的确认动作会有最多 KEY_SEQ_MS 的延迟。
 *    觉得迟钝就把它改小(比如 200); 不需要组合手势就配成 0。
 *
 *    PREV/NEXT 只当编码器用, 不需要组合手势, 所以窗口配 0 -> 零延迟。
 *    这一点很关键: 否则每次翻焦点都要等 300ms, 手感会很差。 */
static const key_gesture_cfg_t s_cfg[KEY_MAX] =
{
    { KEY_LONG_MS, 0         },   /* PREV: 零延迟 */
    { KEY_LONG_MS, 0         },   /* NEXT: 零延迟 */
    { KEY_LONG_MS, KEY_SEQ_MS},   /* ENTER: 支持 5 种手势 */
};

/* ==========================================================================
 *  内部状态
 * ========================================================================== */
static rt_base_t s_pin[KEY_MAX];

/* 消抖 */
static rt_uint8_t s_stable[KEY_MAX];      /* 消抖后的稳定状态, 1=按下 */
static rt_uint8_t s_last_raw[KEY_MAX];
static rt_uint8_t s_cnt[KEY_MAX];

/* 手势识别 */
static key_gesture_ctx_t s_gesture[KEY_MAX];

static struct rt_timer s_key_timer;

/* ==========================================================================
 *  时间戳
 *  状态机要求调用者给单调递增的毫秒时间。用内核 tick 换算,
 *  这样即使某次扫描被延迟, 时间也不会漂。
 * ========================================================================== */
/**
 * @brief 把系统 tick 换算为手势状态机使用的毫秒时间戳。
 * @return 32 位毫秒计数，允许自然回绕。
 * @details 用法：扫描时读取一次，供所有按键使用同一时刻。
 *          动作：用 64 位中间乘法进行 tick 到毫秒换算，避免乘 1000 时提前溢出。
 */
static rt_uint32_t key_now_ms(void)
{
    return (rt_uint32_t)(((rt_uint64_t)rt_tick_get() * 1000u) / RT_TICK_PER_SECOND);
}

/* ==========================================================================
 *  扫描
 * ========================================================================== */
/**
 * @brief 扫描三路低有效按键，执行消抖并推进手势识别。
 * @details 用法：由软件定时器周期调用，不需要在应用中额外循环扫描。
 *          动作：rt_pin_read() 读低电平按下状态；电平变化后重新计数，满足稳定门限才更新状态并喂给状态机。
 * @note 扫描节拍由 KEY_SCAN_MS 决定；实际消抖延时受采样相位和电平变化后的计数过程影响。
 */
static void key_scan(void)
{
    rt_uint32_t now = key_now_ms();
    int i;

    for (i = 0; i < KEY_MAX; i++)
    {
        /* 按键一端接 GND, 内部上拉 -> 按下读到低电平, 这里取反成"1=按下" */
        rt_uint8_t raw = (rt_pin_read(s_pin[i]) == PIN_LOW) ? 1u : 0u;

        /* ---- 消抖: 连续 KEY_DEBOUNCE_CNT 次相同才更新稳定值 ---- */
        if (raw == s_last_raw[i])
        {
            if ((s_cnt[i] < KEY_DEBOUNCE_CNT) && (++s_cnt[i] >= KEY_DEBOUNCE_CNT))
            {
                s_stable[i] = raw;
            }
        }
        else
        {
            s_last_raw[i] = raw;
            s_cnt[i]      = 0u;
        }

        /* ---- 喂给手势状态机 ---- */
        key_gesture_step(&s_gesture[i], s_stable[i], now);
    }
}

/**
 * @brief 按键软件定时器回调，执行一轮扫描。
 * @param parameter 定时器用户参数，未使用。
 * @details 用法：key_init() 自动注册，由 RT-Thread 软件定时器线程调用。
 *          动作：调用 key_scan()，应保持短小，避免阻塞其他软件定时器。
 */
static void key_timer_cb(void *parameter)
{
    (void)parameter;
    key_scan();
}

/* ==========================================================================
 *  对外接口
 * ========================================================================== */
/**
 * @brief 初始化三路按键 GPIO、手势上下文和扫描软件定时器。
 * @return 当前实现执行完返回 RT_EOK。
 * @details 用法：由 INIT_DEVICE_EXPORT 自动调用一次；接线修改 board_config.h，手感参数修改本文件 KEY_* 宏。
 *          动作：配置上拉输入，清消抖状态，设置方向键/确认键的组合窗口，启动 10 ms 周期扫描。
 * @note 按键另一端接 GND；不要再次手动初始化仍在运行的定时器。
 */
int key_init(void)
{
    int i;
    static const rt_base_t pins[KEY_MAX]     = { KEY_PREV_PIN, KEY_NEXT_PIN, KEY_ENTER_PIN };
    static const char *const names[KEY_MAX]  = { KEY_PREV_NAME, KEY_NEXT_NAME, KEY_ENTER_NAME };

    for (i = 0; i < KEY_MAX; i++)
    {
        s_pin[i] = pins[i];

        /* 上拉输入, 按下为低 */
        rt_pin_mode(s_pin[i], PIN_MODE_INPUT_PULLUP);

        s_stable[i]   = 0u;
        s_last_raw[i] = 0u;
        s_cnt[i]      = 0u;

        key_gesture_init(&s_gesture[i], &s_cfg[i]);
    }

    /* 软件定时器周期扫描, 不占线程 */
    rt_timer_init(&s_key_timer, "key",
                  key_timer_cb, RT_NULL,
                  rt_tick_from_millisecond(KEY_SCAN_MS),
                  RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    rt_timer_start(&s_key_timer);

    rt_kprintf("[key] 3 键就绪: PREV=%s NEXT=%s ENTER=%s, 扫描 %dms, "
               "长按 %dms, 组合窗口 %dms\n",
               names[0], names[1], names[2],
               KEY_SCAN_MS, KEY_LONG_MS, KEY_SEQ_MS);
    return RT_EOK;
}
INIT_DEVICE_EXPORT(key_init);

/**
 * @brief 从指定按键的 FIFO 取走一个已确认手势。
 * @param id KEY_PREV、KEY_NEXT 或 KEY_ENTER。
 * @return 手势枚举；队列空或编号无效时返回 KEY_GESTURE_NONE。
 * @details 用法：一般由 LVGL 输入线程读取 ENTER；读取会消费事件，不能让多个模块重复取同一个队列。
 *          动作：检查编号后调用 key_gesture_take()；PREV/NEXT 队列已由 key_take_encoder_diff() 消费。
 */
key_gesture_t key_take_gesture(key_id_t id)
{
    if ((unsigned int)id >= (unsigned int)KEY_MAX)
    {
        return KEY_GESTURE_NONE;
    }
    return key_gesture_take(&s_gesture[id]);
}

/**
 * @brief 查询按键识别器当前的长按活动标志。
 * @param id 按键枚举值。
 * @return 非 0=当前状态机报告长按活动；0=未活动或编号无效。
 * @details 用法：PREV/NEXT 长按连发使用该即时标志，不必等最终手势出队。
 *          动作：只读内部状态；不会消费队列，组合手势的锁定状态可能清除此标志。
 */
rt_uint8_t key_is_long_active(key_id_t id)
{
    if ((unsigned int)id >= (unsigned int)KEY_MAX)
    {
        return 0u;
    }
    return key_gesture_is_long_active(&s_gesture[id]);
}

/**
 * @brief 查询按键消抖后的当前电平状态。
 * @param id 按键枚举值。
 * @return 1=按下，0=松开或编号无效。
 * @details 用法：用于界面的按下/松开视觉反馈，不等同于短按事件。
 *          动作：返回最近一轮扫描的稳定状态，不额外读取 GPIO，不消费手势。
 */
rt_uint8_t key_is_pressed(key_id_t id)
{
    if ((unsigned int)id >= (unsigned int)KEY_MAX)
    {
        return 0u;
    }
    return s_stable[id];
}

/**
 * @brief 把 PREV/NEXT 积累的手势取走并转换为带符号步数。
 * @return NEXT 为正、PREV 为负；没有方向事件时为 0。
 * @details 用法：由 LVGL 输入线程单独消费，其他模块不要同时读取这两个按键的手势队列。
 *          动作：短按计一格、双短按计两格；长按不在这里计步，由 encoder_repeat() 实现持续滚动。
 */
rt_int32_t key_take_encoder_diff(void)
{
    rt_int32_t diff = 0;
    key_gesture_t g;

    /* PREV/NEXT 的 seq 窗口是 0, 所以这里取的必然已经是"定下来"的手势 */
    while ((g = key_gesture_take(&s_gesture[KEY_PREV])) != KEY_GESTURE_NONE)
    {
        if (g == KEY_GESTURE_SHORT)       diff -= 1;
        else if (g == KEY_GESTURE_DOUBLE) diff -= 2;
        /* LONG 不在这里处理: 长按连滚走 key_is_long_active() 的连发路径 */
    }

    while ((g = key_gesture_take(&s_gesture[KEY_NEXT])) != KEY_GESTURE_NONE)
    {
        if (g == KEY_GESTURE_SHORT)       diff += 1;
        else if (g == KEY_GESTURE_DOUBLE) diff += 2;
    }

    return diff;
}

/* ==========================================================================
 *  自检
 *
 *  手势识别器是纯逻辑(只吃"电平 + 毫秒时间戳"), 所以能用【合成的时间序列】
 *  离线验证 —— 不需要真实按键, 也不用掐秒表。
 *
 *  用例集放在 key_gesture_selftest.h, 与 PC 端的 tools/key_gesture_test.c
 *  共用同一份, 保证两边不会跑歪。
 *
 *  ⚠️ 改了状态机, 先在 PC 上跑:
 *      gcc -I board tools/key_gesture_test.c board/key_gesture.c -o kt
 *      ./kt
 *     通过了再烧板子。
 * ========================================================================== */
#define KEY_GESTURE_TEST_PRINT(...)     rt_kprintf(__VA_ARGS__)
#include "key_gesture_selftest.h"

/**
 * @brief 用合成电平/时间序列验证手势状态机。
 * @return 0=全部通过，非 0=失败的用例数。
 * @details 用法：msh 输入 keytest，或由启动自检调用；不需要真实按键。
 *          动作：运行 key_gesture_selftest.h 中共享用例，参数沿用当前长按、组合窗口和扫描周期。
 */
int key_selftest(void)
{
    return key_gesture_test_run(KEY_LONG_MS, KEY_SEQ_MS, KEY_SCAN_MS);
}
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(key_selftest, keytest, run key gesture recognition self test);
#endif
