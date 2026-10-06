/**
 * @file    lv_port_indev.c
 * @brief   LVGL 输入移植层 —— 3 按键模拟"编码器 + 手势"
 *
 *  ── 为什么要自己判手势 ──
 *  LVGL v8.3 的输入系统只提供四种事件:
 *      SHORT_CLICKED / CLICKED / LONG_PRESSED / LONG_PRESSED_REPEAT
 *  它【没有】DOUBLE_CLICKED, 也不支持"长按接短按""短按接长按"这类序列手势。
 *  而且编码器长按在可编辑控件上会被 LVGL 内部吃掉(直接切编辑模式, 不发事件)。
 *  所以这 5 种手势必须在驱动层做 —— 见 board/key_gesture.c。
 *
 *  ── 分工 ──
 *      PREV / NEXT : 完全交给 LVGL 原生编码器处理
 *                    (enc_diff -> 导航模式下换焦点, 编辑模式下发 KEY_LEFT/RIGHT)
 *                    长按额外做"连滚", 这是按键最自然的用法
 *      ENTER       : 由手势层接管。因为要让"两次短按"和"短按"区分开,
 *                    第一次短按必须等满组合窗口才能定案 —— 如果同时让 LVGL
 *                    自己判 CLICKED, 就会先多触发一次动作。
 *                    接管后由本文件按 LVGL 原生逻辑重新派发, 行为一致。
 *
 *  ── 代价 ──
 *      短按/长按的动作会晚 KEY_SEQ_MS(默认 300ms) 才生效。
 *      这是组合手势的固有代价, 绕不过去。
 *      * 按下/抬起的【视觉反馈】是即时的(见 read_cb 里的 PRESSED/RELEASED)
 *      * 不想要这个延迟就把 board/drv_key.c 的 KEY_SEQ_MS 改成 0
 */

#include <rtthread.h>

#include "lvgl.h"
#include "drv_key.h"
#include "lv_port_indev.h"

/* PREV/NEXT 长按连滚: 每次滚动的间隔 */
#define ENC_REPEAT_MS       120u

static lv_indev_t *s_indev;
static lv_group_t *s_group;

static lv_port_indev_gesture_cb_t s_gesture_cb;

/* ENTER 的物理电平边沿跟踪 —— 用来做即时的按下/抬起视觉反馈 */
static rt_uint8_t s_enter_prev;

/* PREV/NEXT 的连滚状态 */
static rt_uint8_t  s_rep_active[2];
static rt_tick_t   s_rep_next[2];

/* ==========================================================================
 *  ENTER: 复刻 LVGL 原生编码器的行为
 *
 *  下面两个函数的判断条件、事件顺序, 都是照 lv_indev.c 的
 *  indev_encoder_proc() 抄的, 保证接管后控件行为与原生一致。
 * ========================================================================== */
/**
 * @brief 取得当前按键焦点所在控件。
 * @return 焦点控件指针；组中没有焦点时返回空指针。
 * @details 用法：输入组创建后由本文件的 LVGL 线程回调使用。
 *          动作：查询 s_group，不改变焦点和控件状态。
 */
static lv_obj_t *focused_obj(void)
{
    return lv_group_get_focused(s_group);
}

/**
 * @brief 判断控件是否适合进入编码器编辑模式。
 * @param obj 非空的 LVGL 控件指针。
 * @return RT_TRUE=可编辑或可滚动，RT_FALSE=普通点击控件。
 * @details 用法：派发 ENTER 手势前先取得有效焦点对象。
 *          动作：检查控件编辑能力及 LV_OBJ_FLAG_SCROLLABLE 标志。
 */
static rt_bool_t is_editable_or_scrollable(lv_obj_t *obj)
{
    return (lv_obj_is_editable(obj) || lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE))
           ? RT_TRUE : RT_FALSE;
}

/**
 * @brief 将已确认的 ENTER 短按转换为点击或进入编辑模式。
 * @details 用法：由 dispatch_enter_gestures() 在 LVGL 线程调用，不能在按键扫描中断中调用。
 *          动作：普通控件收到 SHORT_CLICKED/CLICKED；可编辑控件在导航态进入编辑，在编辑态收到点击及 ENTER。
 * @note 每次事件派发后检查对象是否被事件回调删除，避免继续操作失效对象。
 */
static void dispatch_short(void)
{
    lv_obj_t *f = focused_obj();

    if (f == RT_NULL)
    {
        return;
    }

    if (is_editable_or_scrollable(f) == RT_FALSE)
    {
        /* 不可编辑: 就是一次普通点击 */
        if (lv_event_send(f, LV_EVENT_SHORT_CLICKED, RT_NULL) == LV_RES_INV) return;
        lv_event_send(f, LV_EVENT_CLICKED, RT_NULL);
    }
    else if (lv_group_get_editing(s_group))
    {
        /* 正在编辑: 点击 + 把确认键送给控件自己 */
        if (lv_event_send(f, LV_EVENT_SHORT_CLICKED, RT_NULL) == LV_RES_INV) return;
        if (lv_event_send(f, LV_EVENT_CLICKED, RT_NULL) == LV_RES_INV) return;
        lv_group_send_data(s_group, LV_KEY_ENTER);
    }
    else
    {
        /* 可编辑但还在导航模式: 进入编辑模式 (LVGL 原生行为) */
        lv_group_set_editing(s_group, true);
        lv_obj_clear_state(f, LV_STATE_PRESSED);
    }
}

/**
 * @brief 将已确认的 ENTER 长按转换为编辑模式切换或长按事件。
 * @details 用法：由输入回调在 LVGL 线程调用；手势确认时机由 key_gesture 状态机决定。
 *          动作：可编辑/可滚动控件在组内多于一个对象时切换编辑状态；普通控件收到 LONG_PRESSED。
 */
static void dispatch_long(void)
{
    lv_obj_t *f = focused_obj();

    if (f == RT_NULL)
    {
        return;
    }

    if (is_editable_or_scrollable(f) != RT_FALSE)
    {
        /* 可编辑: 切换编辑模式(至少要有一个控件才谈得上切换) */
        if (lv_group_get_obj_count(s_group) > 1)
        {
            lv_group_set_editing(s_group, lv_group_get_editing(s_group) ? false : true);
            lv_obj_clear_state(f, LV_STATE_PRESSED);
        }
    }
    else
    {
        lv_event_send(f, LV_EVENT_LONG_PRESSED, RT_NULL);
    }
}

/**
 * @brief 消费 ENTER 的手势 FIFO，并按类型派发。
 * @details 用法：由 indev_read() 在 LVGL 线程执行，保持单一消费者。
 *          动作：取完队列中的手势；普通短/长按交给本地派发函数，三种组合手势交给应用回调。
 */
static void dispatch_enter_gestures(void)
{
    key_gesture_t g;

    while ((g = key_take_gesture(KEY_ENTER)) != KEY_GESTURE_NONE)
    {
        switch (g)
        {
            case KEY_GESTURE_SHORT:
                dispatch_short();
                break;

            case KEY_GESTURE_LONG:
                dispatch_long();
                break;

            /* 组合手势 LVGL 原生不支持, 交给应用 */
            case KEY_GESTURE_DOUBLE:
            case KEY_GESTURE_LONG_SHORT:
            case KEY_GESTURE_SHORT_LONG:
                if (s_gesture_cb != RT_NULL)
                {
                    s_gesture_cb(g);
                }
                break;

            default:
                break;
        }
    }
}

/* ==========================================================================
 *  PREV/NEXT: 长按连滚
 * ========================================================================== */
/**
 * @brief 将 PREV/NEXT 的持续长按转换为周期性编码器步进。
 * @param idx PREV=0、NEXT=1；id 为相应按键；diff 为累积步数地址；now 为当前 RT tick。
 * @details 用法：由 indev_read() 对两个方向键各调用一次。
 *          动作：长按开始时建立下次时间，之后每 ENC_REPEAT_MS 增加或减少一步，松开则取消连发。
 */
static void encoder_repeat(int idx, key_id_t id, rt_int32_t *diff, rt_tick_t now)
{
    if (key_is_long_active(id) == 0u)
    {
        s_rep_active[idx] = 0u;
        return;
    }

    if (s_rep_active[idx] == 0u)
    {
        /* 刚进入长按: 起个步, 第一次连滚等一个间隔再发生 */
        s_rep_active[idx] = 1u;
        s_rep_next[idx]   = now + rt_tick_from_millisecond(ENC_REPEAT_MS);
        return;
    }

    if ((rt_int32_t)(now - s_rep_next[idx]) >= 0)
    {
        *diff += (id == KEY_NEXT) ? 1 : -1;
        s_rep_next[idx] = now + rt_tick_from_millisecond(ENC_REPEAT_MS);
    }
}

/* ==========================================================================
 *  读取回调 —— LVGL 每 LV_INDEV_DEF_READ_PERIOD 调一次
 *  运行在 lvgl 线程里(由 lv_timer_handler 调用), 可以安全操作 LVGL 对象。
 * ========================================================================== */
/**
 * @brief LVGL 输入读取回调：提供方向步进并处理 ENTER 手势。
 * @param drv 输入驱动对象；data 为本次输出的 LVGL 输入数据。
 * @details 用法：由 lv_timer_handler() 在 LVGL 线程调用，周期由 LVGL 输入配置决定。
 *          动作：合并方向键短按及长按连发，派发 ENTER 电平视觉反馈，再处理已确认的 ENTER 手势。
 *          最后写 enc_diff，并固定报告 RELEASED，避免 LVGL 把同一次 ENTER 再识别一遍。
 */
static void indev_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    rt_tick_t now = rt_tick_get();
    rt_int32_t diff = 0;

    (void)drv;

    /* --- 1. 编码器步数: 短按一步 + 长按连滚 --- */
    diff += key_take_encoder_diff();
    encoder_repeat(0, KEY_PREV, &diff, now);
    encoder_repeat(1, KEY_NEXT, &diff, now);

    /* --- 2. ENTER 的瞬时视觉反馈 ---
     * 只发 PRESSED/RELEASED 做视觉反馈, 不发 CLICKED ——
     * 动作要等手势判定完(可能是短按, 也可能是两次短按的前半段)。 */
    {
        rt_uint8_t pressed_now = key_is_pressed(KEY_ENTER);
        lv_obj_t  *f = focused_obj();

        if ((f != RT_NULL) && (pressed_now != s_enter_prev))
        {
            lv_event_send(f, pressed_now ? LV_EVENT_PRESSED : LV_EVENT_RELEASED, RT_NULL);
        }
        s_enter_prev = pressed_now;
    }

    /* --- 3. 派发已经定案的手势 --- */
    dispatch_enter_gestures();

    /* --- 4. 报告给 LVGL ---
     * state 恒为 RELEASED: ENTER 由手势层负责, 不能让 LVGL 再判一次点击;
     * 而 enc_diff 在 RELEASED 状态下才会被处理(lv_indev.c 里明确如此)。 */
    data->enc_diff = (rt_int16_t)diff;
    data->state    = LV_INDEV_STATE_RELEASED;
    data->key      = LV_KEY_ENTER;
}

/* ==========================================================================
 *  初始化
 * ========================================================================== */
/**
 * @brief 注册按键模拟编码器并创建默认焦点组。
 * @details 用法：lv_init() 之后在 LVGL 线程调用一次；按键 GPIO/扫描由 key_init() 自动初始化。
 *          动作：绑定 indev_read()，创建并设置默认 group，把编码器挂到该组并开启循环导航。
 * @note 控件需加入 group 才能被按键选择；本函数没有重复初始化保护。
 */
void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type                 = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb              = indev_read;
    /* ENTER 不走 LVGL 的长按计时(手势层自己判), 这两个值对 ENTER 无影响,
     * 但保留默认值以便将来切回原生模式 */
    s_indev = lv_indev_drv_register(&indev_drv);

    s_group = lv_group_create();
    lv_group_set_default(s_group);
    lv_indev_set_group(s_indev, s_group);
    lv_group_set_wrap(s_group, true);

    rt_kprintf("[lvgl] 输入设备: encoder + 手势 (PREV/NEXT 导航+长按连滚, "
               "ENTER 短按/长按/双击/长接短/短接长)\n");
}

/**
 * @brief 取得已注册的 LVGL 编码器输入对象。
 * @return 对象指针；初始化前为 RT_NULL，由 LVGL 管理其生命周期。
 * @details 用法：在 LVGL 线程查询或配置输入设备，调用者不要自行释放。
 *          动作：返回保存的 s_indev，不执行硬件读取。
 */
lv_indev_t *lv_port_indev_get(void)
{
    return s_indev;
}

/**
 * @brief 取得按键导航使用的默认控件组。
 * @return 组对象指针；初始化前为 RT_NULL。
 * @details 用法：在 LVGL 线程使用 lv_group_add_obj(lv_port_indev_get_group(), obj) 添加可操作控件。
 *          动作：返回 s_group；页面切换时需维护组内对象，使焦点只落在可见控件上。
 */
lv_group_t *lv_port_indev_get_group(void)
{
    return s_group;
}

/**
 * @brief 注册应用的 ENTER 组合手势回调。
 * @param cb 回调函数，传 RT_NULL 取消回调。
 * @details 用法：在 LVGL 线程初始化或切换页面模式时设置；回调也在 LVGL 线程执行。
 *          动作：保存函数指针，双短按/长接短/短接长时转交应用；普通短按和长按仍由本移植层派发。
 */
void lv_port_indev_set_gesture_cb(lv_port_indev_gesture_cb_t cb)
{
    s_gesture_cb = cb;
}
