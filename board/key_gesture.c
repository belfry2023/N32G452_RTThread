/**
 * @file    key_gesture.c
 * @brief   按键手势识别器实现 —— 纯逻辑, 见 key_gesture.h 的说明
 */

#include "key_gesture.h"

/* ==========================================================================
 *  状态机状态
 *
 *      IDLE ──按下──> DOWN ──松开──> WAIT ──超时──> 发 SHORT / LONG ──> IDLE
 *                       │              │
 *                       │              └──窗口内又按下──> DOWN2
 *                       │                                    │
 *                       └─(按住≥long_ms, long_active=1)       ├─松开(短)──> 发组合手势 ──> IDLE
 *                                                            └─按住≥long_ms─> 发组合手势 ──> LOCK
 *
 *  LOCK 状态的作用:
 *      "先短按接长按"是在按住的过程中就判定完成的(这样反馈更及时),
 *      但手指还没松开。如果没有 LOCK, 状态机回到 IDLE 后又看到 pressed=1,
 *      会把同一次按住当成一次全新的按下 —— 松手时就会多出一个假短按。
 *      LOCK 就是"手势已产出, 等松手再回 IDLE"。
 * ========================================================================== */
enum
{
    SM_IDLE = 0,
    SM_DOWN,
    SM_WAIT,
    SM_DOWN2,
    SM_LOCK
};

/* 默认时间参数 */
#define KEY_GESTURE_DEF_LONG_MS     600u
#define KEY_GESTURE_DEF_SEQ_MS      300u

/* ==========================================================================
 *  事件队列 (环形)
 * ========================================================================== */
/**
 * @brief 将已确认手势放入单个识别器的环形队列。
 * @param ctx 有效上下文；g 为要入队的手势。
 * @details 用法：状态机内部调用；队列访问的并发约束由使用者负责。
 *          动作：计算下一个队头，满时丢弃最旧项，再写入新事件并推进队头。
 */
static void q_push(key_gesture_ctx_t *ctx, key_gesture_t g)
{
    uint8_t next = (uint8_t)((ctx->q_head + 1u) % KEY_GESTURE_QUEUE_LEN);

    if (next == ctx->q_tail)
    {
        /* 队列满: 丢掉最旧的一个, 保证新事件不丢 */
        ctx->q_tail = (uint8_t)((ctx->q_tail + 1u) % KEY_GESTURE_QUEUE_LEN);
    }

    ctx->queue[ctx->q_head] = (uint8_t)g;
    ctx->q_head = next;
}

/**
 * @brief 取走识别器队列中最旧的一条手势。
 * @param ctx 已初始化、非空的上下文。
 * @return 手势值；队列空返回 KEY_GESTURE_NONE。
 * @details 用法：由唯一消费者轮询；本纯逻辑模块不提供多线程互斥。
 *          动作：读取队尾事件并推进队尾，不推进识别状态机。
 */
key_gesture_t key_gesture_take(key_gesture_ctx_t *ctx)
{
    key_gesture_t g;

    if (ctx->q_tail == ctx->q_head)
    {
        return KEY_GESTURE_NONE;
    }

    g = (key_gesture_t)ctx->queue[ctx->q_tail];
    ctx->q_tail = (uint8_t)((ctx->q_tail + 1u) % KEY_GESTURE_QUEUE_LEN);
    return g;
}

/**
 * @brief 清空积压的手势事件队列。
 * @param ctx 已初始化、非空的上下文。
 * @details 用法：需要丢弃旧事件时调用，不能与同一上下文的 step/take 并发执行。
 *          动作：把队头和队尾归零；不会复位当前按压状态，完全重置请调用 key_gesture_init()。
 */
void key_gesture_flush(key_gesture_ctx_t *ctx)
{
    ctx->q_head = 0;
    ctx->q_tail = 0;
}

/* ==========================================================================
 *  初始化
 * ========================================================================== */
/**
 * @brief 初始化一个与硬件无关的按键手势识别器。
 * @param ctx 可写的非空上下文；cfg 为毫秒时间参数，传空指针使用 {600,300}。
 * @details 用法：每个按键创建独立 ctx，初始化后周期调用 key_gesture_step()。
 *          动作：复制时间配置，清状态/时间戳/长按标志，并清空队列；本函数不读取 GPIO。
 */
void key_gesture_init(key_gesture_ctx_t *ctx, const key_gesture_cfg_t *cfg)
{
    if (cfg != 0)
    {
        ctx->cfg = *cfg;
    }
    else
    {
        ctx->cfg.long_ms = KEY_GESTURE_DEF_LONG_MS;
        ctx->cfg.seq_ms  = KEY_GESTURE_DEF_SEQ_MS;
    }

    ctx->state       = SM_IDLE;
    ctx->t_down      = 0;
    ctx->t_wait      = 0;
    ctx->first_long  = 0;
    ctx->long_active = 0;

    key_gesture_flush(ctx);
}

/* ==========================================================================
 *  状态机
 * ========================================================================== */
/**
 * @brief 根据已消抖电平和当前时间推进一次手势状态机。
 * @param ctx 非空上下文；pressed 为 1=按下、0=松开；now_ms 为连续采样的毫秒时间戳。
 * @return 本次产生的事件数量 0、1 或 2；事件仍需用 key_gesture_take() 取出。
 * @details 用法：按固定扫描节拍调用，即使电平不变也要调用，以识别长按和窗口超时。
 *          动作：IDLE 等按下，DOWN 记录持续时长，WAIT 等组合窗口，DOWN2 识别第二段，LOCK 等松手避免重复上报。
 *          第一段短/长按通常松手后等待窗口；第二段长按可在按住时确认；两次长按可拆成两条事件。
 * @note 本函数只处理逻辑，不做 GPIO 消抖；不要在多个线程中同时推进同一个 ctx。
 */
int key_gesture_step(key_gesture_ctx_t *ctx, uint8_t pressed, uint32_t now_ms)
{
    int produced = 0;

    switch (ctx->state)
    {
    /* ---------------------------------------------------------------- */
    case SM_IDLE:
        ctx->long_active = 0;
        if (pressed)
        {
            ctx->state  = SM_DOWN;
            ctx->t_down = now_ms;
        }
        break;

    /* ---------------------------------------------------------------- */
    case SM_DOWN:
        if (pressed)
        {
            /* 按住超过阈值: 只是点亮"长按进行中"标志(给连发/连滚用),
             * 手势本身还不能发 —— 因为可能后面还接一次短按 */
            if ((ctx->long_active == 0u) &&
                ((uint32_t)(now_ms - ctx->t_down) >= ctx->cfg.long_ms))
            {
                ctx->long_active = 1u;
            }
        }
        else
        {
            ctx->first_long = ((uint32_t)(now_ms - ctx->t_down) >= ctx->cfg.long_ms)
                              ? 1u : 0u;
            ctx->long_active = 0u;
            ctx->state       = SM_WAIT;
            ctx->t_wait      = now_ms;
        }
        break;

    /* ---------------------------------------------------------------- */
    case SM_WAIT:
        if (pressed)
        {
            /* 窗口内又按下了 -> 可能是组合手势 */
            ctx->state  = SM_DOWN2;
            ctx->t_down = now_ms;
        }
        else if ((uint32_t)(now_ms - ctx->t_wait) >= ctx->cfg.seq_ms)
        {
            /* 窗口内没有第二次按下 -> 就发单个手势
             * (seq_ms = 0 时这里下一个 tick 就会命中 -> 零延迟) */
            q_push(ctx, ctx->first_long ? KEY_GESTURE_LONG : KEY_GESTURE_SHORT);
            produced++;
            ctx->state = SM_IDLE;
        }
        break;

    /* ---------------------------------------------------------------- */
    case SM_DOWN2:
        if (pressed)
        {
            if ((uint32_t)(now_ms - ctx->t_down) >= ctx->cfg.long_ms)
            {
                /* 第二次也是长按: 按住过程中就判定, 反馈更及时 */
                ctx->long_active = 1u;

                if (ctx->first_long)
                {
                    /* "长按接长按"不在规格里, 拆成两个独立的长按 */
                    q_push(ctx, KEY_GESTURE_LONG);
                    q_push(ctx, KEY_GESTURE_LONG);
                    produced += 2;
                }
                else
                {
                    q_push(ctx, KEY_GESTURE_SHORT_LONG);
                    produced++;
                }

                ctx->state = SM_LOCK;   /* 等松手, 否则这次按住会被当成新的一次 */
            }
        }
        else
        {
            uint8_t second_long = ((uint32_t)(now_ms - ctx->t_down) >= ctx->cfg.long_ms)
                                  ? 1u : 0u;

            if (ctx->first_long)
            {
                q_push(ctx, second_long ? KEY_GESTURE_LONG : KEY_GESTURE_LONG_SHORT);
            }
            else
            {
                q_push(ctx, second_long ? KEY_GESTURE_SHORT_LONG : KEY_GESTURE_DOUBLE);
            }
            produced++;
            ctx->long_active = 0u;
            ctx->state       = SM_IDLE;
        }
        break;

    /* ---------------------------------------------------------------- */
    case SM_LOCK:
        /* 手势已经产出, 只是等手指松开 */
        ctx->long_active = 0u;
        if (!pressed)
        {
            ctx->state = SM_IDLE;
        }
        break;

    default:
        ctx->state = SM_IDLE;
        break;
    }

    return produced;
}

/* ==========================================================================
 *  查询
 * ========================================================================== */
/**
 * @brief 读取当前状态机的长按活动标志。
 * @param ctx 非空的识别器上下文。
 * @return long_active 标志，非 0 表示长按活动。
 * @details 用法：供方向键连发判断；它不是最终手势事件。
 *          动作：直接读取字段，不修改状态或消费队列。
 */
uint8_t key_gesture_is_long_active(const key_gesture_ctx_t *ctx)
{
    return ctx->long_active;
}

/**
 * @brief 返回手势的固定英文名称，方便串口诊断。
 * @param g 手势枚举。
 * @return 静态字符串；无手势或未知值返回 NONE，调用者不得释放或修改。
 * @details 用法：可传给 rt_kprintf() 等打印接口。
 *          动作：按枚举选择字符串，不分配内存、不访问硬件。
 */
const char *key_gesture_name(key_gesture_t g)
{
    switch (g)
    {
        case KEY_GESTURE_SHORT:      return "SHORT";
        case KEY_GESTURE_LONG:       return "LONG";
        case KEY_GESTURE_DOUBLE:     return "DOUBLE";
        case KEY_GESTURE_LONG_SHORT: return "LONG+SHORT";
        case KEY_GESTURE_SHORT_LONG: return "SHORT+LONG";
        default:                     return "NONE";
    }
}
