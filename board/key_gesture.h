/**
 * @file    key_gesture.h
 * @brief   按键手势识别器 —— 纯逻辑, 不依赖 RT-Thread / 硬件
 *
 *  支持 5 种手势:
 *      短按            SHORT          按下后很快松开
 *      长按            LONG           按住超过 long_ms 再松开
 *      两次短按        DOUBLE         短按 -> 短按
 *      先长按接短按    LONG_SHORT     长按 -> 短按
 *      先短按接长按    SHORT_LONG     短按 -> 长按
 *
 *  ── 为什么单独一个文件 ──
 *  这里只有状态机, 不碰 GPIO、不碰内核、不调 rt_kprintf。
 *  于是它可以:
 *      1. 在 PC 上直接编译做单元测试 (不用板子)
 *      2. 被别的按键/别的板子复用
 *      3. 用合成的时间序列验证 —— 见 drv_key.c 的 key_selftest()
 *  drv_key.c 负责"读引脚 + 消抖", 把消抖后的电平喂给这里。
 *
 *  ── 时序约定 ──
 *  输入必须是【已消抖】的电平, 每 SCAN_MS 调一次 step()。
 *  step() 里的时间戳由调用者给, 状态机内部不取时间 —— 这样才好测。
 *
 *  时间轴示意 (cfg = {long_ms=600, seq_ms=300}):
 *
 *    短按:      ┌──┐
 *               ┘  └──────────────          -> 松手后等满 seq_ms 才发 SHORT
 *              0  80
 *
 *    长按:      ┌──────────────┐
 *               ┘              └──────      -> 松手后等满 seq_ms 才发 LONG
 *              0            800
 *
 *    两次短按:  ┌─┐   ┌─┐
 *               ┘ └───┘ └─────               -> 第二次松手立刻发 DOUBLE
 *              0 80  180 260
 *
 *  ⚠️ 关键设计: 第一个手势必须【等满 seq_ms】才能确定。
 *     因为"短按"和"两次短按"的前半段完全一样, 不等就没法区分。
 *     这就是组合手势的固有延迟, 无法绕过。
 *     => 不需要组合手势的按键, 把 seq_ms 配成 0, 就变成零延迟。
 */
#ifndef __KEY_GESTURE_H__
#define __KEY_GESTURE_H__

#include <stdint.h>

/* ==========================================================================
 *  手势类型
 * ========================================================================== */
typedef enum
{
    KEY_GESTURE_NONE = 0,       /* 无事件(内部使用) */
    KEY_GESTURE_SHORT,          /* 短按 */
    KEY_GESTURE_LONG,           /* 长按 */
    KEY_GESTURE_DOUBLE,         /* 两次短按 */
    KEY_GESTURE_LONG_SHORT,     /* 先长按, 接短按 */
    KEY_GESTURE_SHORT_LONG,     /* 先短按, 接长按 */
    KEY_GESTURE_MAX
} key_gesture_t;

/* ==========================================================================
 *  时间参数
 * ========================================================================== */
typedef struct
{
    uint16_t long_ms;       /* 长按判定阈值。小于它算短按。典型 500~800 */
    uint16_t seq_ms;        /* 组合手势窗口: 松手后等多久看有没有第二次按下。
                             * 0 = 不做组合, 零延迟;
                             * 典型 250~400。此窗口从松手起计，与长按阈值独立 */
} key_gesture_cfg_t;

/* ==========================================================================
 *  事件队列
 *  一次 step() 最多可能产出 2 个手势(长按接长按会拆成两个), 队列留点余量。
 * ========================================================================== */
#define KEY_GESTURE_QUEUE_LEN   4

typedef struct
{
    key_gesture_cfg_t cfg;

    uint8_t  state;             /* 内部状态, 见 .c 里的 SM_xxx */
    uint32_t t_down;            /* 本次按下时刻 */
    uint32_t t_wait;            /* 进入等待窗口的时刻 */
    uint8_t  first_long;        /* 第一个手势是不是长按 */
    uint8_t  long_active;       /* 当前已经按住超过 long_ms (供"连滚/连发"用) */

    uint8_t  queue[KEY_GESTURE_QUEUE_LEN];
    uint8_t  q_head;
    uint8_t  q_tail;
} key_gesture_ctx_t;

/**
 * @brief 初始化一个与硬件无关的按键手势识别器。
 * @param ctx 可写的非空上下文；cfg 为毫秒时间参数，传空指针使用 {600,300}。
 * @details 用法：每个按键创建独立 ctx，初始化后周期调用 key_gesture_step()。
 *          动作：复制时间配置，清状态/时间戳/长按标志，并清空队列；本函数不读取 GPIO。
 */
void key_gesture_init(key_gesture_ctx_t *ctx, const key_gesture_cfg_t *cfg);

/**
 * @brief 根据已消抖电平和当前时间推进一次手势状态机。
 * @param ctx 非空上下文；pressed 为 1=按下、0=松开；now_ms 为连续采样的毫秒时间戳。
 * @return 本次产生的事件数量 0、1 或 2；事件仍需用 key_gesture_take() 取出。
 * @details 用法：按固定扫描节拍调用，即使电平不变也要调用，以识别长按和窗口超时。
 *          动作：IDLE 等按下，DOWN 记录持续时长，WAIT 等组合窗口，DOWN2 识别第二段，LOCK 等松手避免重复上报。
 *          第一段短/长按通常松手后等待窗口；第二段长按可在按住时确认；两次长按可拆成两条事件。
 * @note 本函数只处理逻辑，不做 GPIO 消抖；不要在多个线程中同时推进同一个 ctx。
 */
int key_gesture_step(key_gesture_ctx_t *ctx, uint8_t pressed, uint32_t now_ms);

/**
 * @brief 取走识别器队列中最旧的一条手势。
 * @param ctx 已初始化、非空的上下文。
 * @return 手势值；队列空返回 KEY_GESTURE_NONE。
 * @details 用法：由唯一消费者轮询；本纯逻辑模块不提供多线程互斥。
 *          动作：读取队尾事件并推进队尾，不推进识别状态机。
 */
key_gesture_t key_gesture_take(key_gesture_ctx_t *ctx);

/**
 * @brief 清空积压的手势事件队列。
 * @param ctx 已初始化、非空的上下文。
 * @details 用法：需要丢弃旧事件时调用，不能与同一上下文的 step/take 并发执行。
 *          动作：把队头和队尾归零；不会复位当前按压状态，完全重置请调用 key_gesture_init()。
 */
void key_gesture_flush(key_gesture_ctx_t *ctx);

/**
 * @brief 读取当前状态机的长按活动标志。
 * @param ctx 非空的识别器上下文。
 * @return long_active 标志，非 0 表示长按活动。
 * @details 用法：供方向键连发判断；它不是最终手势事件。
 *          动作：直接读取字段，不修改状态或消费队列。
 */
uint8_t key_gesture_is_long_active(const key_gesture_ctx_t *ctx);

/**
 * @brief 返回手势的固定英文名称，方便串口诊断。
 * @param g 手势枚举。
 * @return 静态字符串；无手势或未知值返回 NONE，调用者不得释放或修改。
 * @details 用法：可传给 rt_kprintf() 等打印接口。
 *          动作：按枚举选择字符串，不分配内存、不访问硬件。
 */
const char *key_gesture_name(key_gesture_t g);

#endif /* __KEY_GESTURE_H__ */
