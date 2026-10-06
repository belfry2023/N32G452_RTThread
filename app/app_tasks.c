/** RT-Thread GP21 consumer and LVGL UI. All device access uses rt_device_*.
 * One GP21 completion creates one sample; repeated reads never invent samples.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "app_tasks.h"
#include "app_led.h"
#include "drv_gp21.h"
#include "board_config.h"
#include "drv_key.h"
#ifndef APP_USE_LVGL
#define APP_USE_LVGL 0
#endif
#if APP_USE_LVGL
#include "lvgl.h"
#include "lv_port_disp.h"
#include "drv_lcd_device.h"
#include "lv_port_indev.h"
#include "demos/lv_demos.h"
#endif

#define UI_PERIOD_MS 16u
static rt_device_t s_tdc;
static struct rt_semaphore s_ready;
static app_result_t s_result;
static struct rt_mutex result_mutex;
static struct rt_mutex *s_result_lock = &result_mutex;
static struct rt_thread s_proc_thread, s_lvgl_thread;
ALIGN(RT_ALIGN_SIZE) static rt_uint8_t s_proc_stack[APP_STACK_PROC];
ALIGN(RT_ALIGN_SIZE) static rt_uint8_t s_lvgl_stack[APP_STACK_LVGL];
static volatile rt_uint32_t s_tdc_ok_count;
static volatile rt_uint8_t s_clear_requested;
/**
 * @brief GP21 新记录通知：唤醒采集处理线程。
 * @param dev 通知来源设备；size 为本次通知的字节数，本回调均不使用。
 * @return rt_sem_release() 的执行结果。
 * @details 用法：由 rt_device_set_rx_indicate() 注册，GP21 工作线程在入队后调用。
 *          动作：仅释放 s_ready 信号量，实际数据由 proc_thread_entry() 读取；此处不操作界面。
 */
static rt_err_t sample_ready(rt_device_t dev, rt_size_t size)
{
    (void)dev; (void)size;
    return rt_sem_release(&s_ready);
}
/**
 * @brief 持续读取 GP21 记录，并统计测量值、最值、有效数量和采样率。
 * @param arg 线程启动参数，本工程传 RT_NULL。
 * @details 用法：由 app_tasks_init() 创建并启动，不作为普通函数直接调用。
 *          动作：查找并打开 tdc0，注册通知并启动测量；失败时延时 2 秒重试。
 *          收到通知或等待超时后排空设备 FIFO，把有效记录的皮秒值换算为微秒，在互斥锁内更新共享结果。
 *          每秒更新有效采样率；清零请求也在本线程处理，避免界面线程直接改统计数据。
 * @note 修改采样后的物理量换算可从这里入手；空 FIFO 不产生新样本，不能靠重复读取增加采样数。
 */
static void proc_thread_entry(void *arg)
{
    (void)arg;
    rt_tick_t window = rt_tick_get();
    rt_uint32_t window_count = 0;
    gp21_sample_t sample;
    for (;;) {
        if (!s_tdc || s_tdc->ref_count == 0) {
            s_tdc = rt_device_find(BSP_TDC_DEVICE_NAME);
            if (!s_tdc || rt_device_open(s_tdc, RT_DEVICE_OFLAG_RDONLY) != RT_EOK) {
                rt_kprintf("[gp21] probe failed; check wiring, power and SPI mode\n");
                rt_thread_mdelay(2000);
                continue;
            }
            rt_device_set_rx_indicate(s_tdc, sample_ready);
            if (rt_device_control(s_tdc, GP21_CTRL_START, RT_NULL) != RT_EOK) {
                rt_device_close(s_tdc);
                rt_thread_mdelay(2000);
                continue;
            }
            rt_kprintf("[gp21] verified ID; range 2 external START/STOP1, ref=%u Hz\n",
                       (unsigned)BSP_TDC_REF_HZ);
        }
        rt_sem_take(&s_ready, rt_tick_from_millisecond(100));
        rt_mutex_take(s_result_lock, RT_WAITING_FOREVER);
        if (s_clear_requested) {
            rt_memset(&s_result, 0, sizeof(s_result));
            s_clear_requested = 0;
            window_count = 0;
            window = rt_tick_get();
        }
        rt_mutex_release(s_result_lock);
        while (rt_device_read(s_tdc, 0, &sample, sizeof(sample)) == sizeof(sample)) {
            rt_mutex_take(s_result_lock, RT_WAITING_FOREVER);
            if (sample.valid) {
                double us = (double)sample.time_ps / 1000000.0;
                if (s_result.count == 0 || us < s_result.value_min) s_result.value_min = us;
                if (s_result.count == 0 || us > s_result.value_max) s_result.value_max = us;
                s_result.value = us;
                s_result.count++;
                s_tdc_ok_count++;
                window_count++;
            } else s_result.bad++;
            s_result.bursts++;
            rt_mutex_release(s_result_lock);
        }
        rt_tick_t now = rt_tick_get();
        if (now - window >= RT_TICK_PER_SECOND) {
            rt_mutex_take(s_result_lock, RT_WAITING_FOREVER);
            s_result.rate_hz = window_count * RT_TICK_PER_SECOND / (now - window);
            rt_mutex_release(s_result_lock);
            window_count = 0;
            window = now;
        }
    }
}

/* ==========================================================================
 *  线程 3: 显示 @ ~60Hz  (优先级 16)
 *
 *  硬件绑定在 board/lv_port_disp.c 与 board/lv_port_indev.c,
 *  本文件只负责界面内容与数据更新。
 * ========================================================================== */
#if APP_USE_LVGL

/* ---- 界面对象 ---- */
static volatile rt_uint8_t s_demo_requested;
static rt_uint8_t s_demo_active;
static volatile rt_uint8_t s_ui_ready;
static volatile rt_uint8_t s_lcd_test_request; /* 1=color bars, 2=resume */
static rt_uint8_t s_lcd_test_active;
static lv_obj_t *s_home, *s_monitor, *s_diag, *s_diag_label;
static lv_obj_t *s_menu_buttons[2], *s_back, *s_diag_back;
static void ui_show_page(unsigned page);
static lv_obj_t *s_label_value;
static lv_obj_t *s_label_rate;
static lv_obj_t *s_label_msg;
static lv_obj_t *s_label_gest;      /* 5 种手势的计数 */
static lv_obj_t *s_slider;
static lv_obj_t *s_switch;
static lv_obj_t *s_roller;
static lv_obj_t *s_btn;

/* ---- 界面状态 (被按键操作改变) ---- */
static double   s_threshold = 1.0;    /* 滑块设定: 超过就标红 */
static rt_uint8_t s_scale_idx = 0;    /* 滚轮设定: 显示倍率 */
static const double s_scale_tab[3] = { 1.0, 10.0, 100.0 };

/* ---- 手势统计 ----
 * 5 种手势各自计一次数, 显示在屏幕底部, 一眼就能看出识别对不对。
 * 这是没有硬件探针时最直接的验证手段。 */
static rt_uint32_t   s_gest_cnt[KEY_GESTURE_MAX];
static key_gesture_t s_gest_last  = KEY_GESTURE_NONE;
static rt_uint8_t    s_gest_dirty = 1u;
static rt_uint8_t    s_paused;        /* "先短按接长按" 切换暂停/继续 */

/**
 * @brief 修改测量页的提示文字。
 * @param text 有效的零结尾字符串，LVGL 会复制文本。
 * @details 用法：在 LVGL 线程的事件回调中调用；标签未创建时不执行操作。
 *          动作：用 lv_label_set_text() 更新 s_label_msg，绘制由后续 LVGL 刷新完成。
 */
static void ui_set_msg(const char *text)
{
    if (s_label_msg != RT_NULL)
    {
        lv_label_set_text(s_label_msg, text);
    }
}

/* ==========================================================================
 *  组合手势处理
 *
 *  ⚠️ 这个回调在 lvgl 线程里被 lv_timer_handler() 调用, 所以可以安全操作控件。
 *     短按 / 长按 不经过这里 —— 它们由 board/lv_port_indev.c 直接派发给 LVGL
 *     (复刻原生编码器行为: 点击 / 进编辑模式), 保证控件行为跟原生一致。
 * ========================================================================== */
/**
 * @brief 把 ENTER 组合手势转换为应用操作。
 * @param g 已确认的双短按、长接短或短接长手势。
 * @details 用法：由输入移植层在 LVGL 线程中回调，可在这里扩展按键功能。
 *          动作：双短按请求清零并回菜单；长接短切换显示倍率；短接长切换数值刷新暂停。
 * @note 暂停只冻结界面数值，GP21 采集继续运行；普通短按/长按由输入移植层处理。
 */
static void app_gesture_cb(key_gesture_t g)
{
    if ((g > KEY_GESTURE_NONE) && (g < KEY_GESTURE_MAX))
    {
        s_gest_cnt[g]++;
    }
    s_gest_last  = g;
    s_gest_dirty = 1u;

    switch (g)
    {
        /* ---- 两次短按 -> 清零统计 ---- */
        case KEY_GESTURE_DOUBLE:
            s_clear_requested = 1;
            ui_show_page(0);
            ui_set_msg("DOUBLE -> back to menu");
            break;

        /* ---- 先长按接短按 -> 循环显示倍率 ---- */
        case KEY_GESTURE_LONG_SHORT:
            s_scale_idx = (rt_uint8_t)((s_scale_idx + 1u) % 3u);
            if (s_roller != RT_NULL)
            {
                lv_roller_set_selected(s_roller, s_scale_idx, LV_ANIM_OFF);
            }
            ui_set_msg("LONG+SHORT -> scale x1/x10/x100");
            break;

        /* ---- 先短按接长按 -> 暂停/继续刷新数值 ---- */
        case KEY_GESTURE_SHORT_LONG:
            s_paused = (rt_uint8_t)((s_paused == 0u) ? 1u : 0u);
            ui_set_msg(s_paused ? "SHORT+LONG -> PAUSED" : "SHORT+LONG -> RUNNING");
            break;

        default:
            break;
    }
}

/* ---- 事件回调: 这些函数只在 lvgl 线程里被 lv_timer_handler() 调用 ---- */
/**
 * @brief 处理 CLEAR 按钮点击。
 * @param e LVGL 事件对象，由 LVGL 提供。
 * @details 用法：通过 lv_obj_add_event_cb() 绑定按钮，只在 LVGL 线程执行。
 *          动作：确认 LV_EVENT_CLICKED 后提交统计清零请求，并更新提示文字。
 */
static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        /* 演示: 按键确认 -> 清零统计 */
        s_clear_requested = 1;
        ui_set_msg("counters cleared");
    }
}

/**
 * @brief 把滑块数值转换为测量值变色阈值。
 * @param e LVGL 数值变化事件，目标对象为滑块。
 * @details 用法：修改滑块联动规则时调整本回调；在 LVGL 线程执行。
 *          动作：取滑块整数值并除以 100，写入 s_threshold；比较使用未乘显示倍率的微秒值。
 */
static void slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        s_threshold = (double)lv_slider_get_value(lv_event_get_target(e)) / 100.0;
        ui_set_msg("threshold changed");
    }
}

/**
 * @brief 用界面开关控制三路 LED 呼吸效果。
 * @param e LVGL 数值变化事件，目标对象为开关。
 * @details 用法：由 LVGL 线程回调，按钮勾选状态决定呼吸开关。
 *          动作：读取 LV_STATE_CHECKED，调用 app_led_breath()，然后显示操作结果。
 */
static void switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        rt_bool_t on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        app_led_breath(on);
        ui_set_msg(on ? "switch: ON" : "switch: OFF");
    }
}

/**
 * @brief 用滚轮选择测量值的显示倍率。
 * @param e LVGL 数值变化事件，目标对象为滚轮。
 * @details 用法：由 LVGL 线程回调；倍率表 s_scale_tab 与滚轮选项需同步修改。
 *          动作：读取选项索引并限制在 0..2，后续显示时使用 x1、x10 或 x100，不修改原始结果。
 */
static void roller_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        s_scale_idx = (rt_uint8_t)lv_roller_get_selected(lv_event_get_target(e));
        if (s_scale_idx > 2u)
        {
            s_scale_idx = 0u;
        }
        ui_set_msg("scale changed");
    }
}

/**
 * @brief 创建 240×320 测量页面及其交互控件。
 * @details 用法：由 ui_init() 在 LVGL 线程中调用一次；改布局、文字、颜色和事件绑定主要修改这里。
 *          动作：创建数值/采样率标签、滑块、开关、清零按钮、倍率滚轮和手势计数标签。
 *          为控件绑定回调并加入输入 group，初始焦点设为滑块。
 * @note 新增可操作控件还需加入 ui_show_page() 对应页面的 group，否则切页后按键选不到它。
 */
static void ui_create(void)
{
    lv_obj_t *scr = lv_obj_create(lv_scr_act());
    s_monitor = scr;
    lv_obj_set_size(scr, 240, 320);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_t *title;
    lv_group_t *g = lv_port_indev_get_group();

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* --- 标题 --- */
    title = lv_label_create(scr);
    lv_label_set_text(title, "N32G452  TDC-GP21");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    /* --- 主数值 --- */
    s_label_value = lv_label_create(scr);
    lv_obj_set_style_text_font(s_label_value, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_label_value, "--.---");
    lv_obj_align(s_label_value, LV_ALIGN_TOP_MID, 0, 24);

    /* --- 采样率 --- */
    s_label_rate = lv_label_create(scr);
    lv_label_set_text(s_label_rate, "0 Hz");
    lv_obj_align(s_label_rate, LV_ALIGN_TOP_MID, 0, 60);

    /* --- 最近一次手势 --- */
    s_label_msg = lv_label_create(scr);
    lv_label_set_text(s_label_msg, "PREV/NEXT move, ENTER act");
    lv_obj_set_style_text_color(s_label_msg, lv_color_hex(0x808080), 0);
    lv_obj_align(s_label_msg, LV_ALIGN_TOP_MID, 0, 80);

    /* --- 滑块: 调阈值 --- */
    s_slider = lv_slider_create(scr);
    lv_obj_set_width(s_slider, 180);
    lv_slider_set_range(s_slider, 0, 500);
    lv_slider_set_value(s_slider, 100, LV_ANIM_OFF);
    lv_obj_align(s_slider, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_add_event_cb(s_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_group_add_obj(g, s_slider);

    /* --- 开关 --- */
    s_switch = lv_switch_create(scr);
    if (app_led_breath_is_on()) lv_obj_add_state(s_switch, LV_STATE_CHECKED);
    lv_obj_align(s_switch, LV_ALIGN_TOP_MID, -60, 130);
    lv_obj_add_event_cb(s_switch, switch_event_cb, LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_group_add_obj(g, s_switch);

    /* --- 按钮 --- */
    s_btn = lv_btn_create(scr);
    lv_obj_set_size(s_btn, 90, 36);
    lv_obj_align(s_btn, LV_ALIGN_TOP_MID, 55, 126);
    lv_obj_add_event_cb(s_btn, btn_event_cb, LV_EVENT_CLICKED, RT_NULL);
    {
        lv_obj_t *l = lv_label_create(s_btn);
        lv_label_set_text(l, "CLEAR");
        lv_obj_center(l);
    }
    lv_group_add_obj(g, s_btn);

    /* --- 滚轮: 选显示倍率 --- */
    s_roller = lv_roller_create(scr);
    lv_roller_set_options(s_roller, "x1\nx10\nx100", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller, 2);
    lv_obj_set_width(s_roller, 90);
    lv_obj_align(s_roller, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_add_event_cb(s_roller, roller_event_cb, LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_group_add_obj(g, s_roller);

    /* --- 5 种手势的计数 ---
     * "S0 L0 D0 LS0 SL0" = 短按 / 长按 / 两次短按 / 长按接短按 / 短按接长按
     * 按键试一遍, 数字应该跟着涨 —— 一眼就能验证识别对不对。 */
    s_label_gest = lv_label_create(scr);
    lv_obj_set_style_text_font(s_label_gest, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label_gest, lv_color_hex(0x00C0FF), 0);
    lv_label_set_text(s_label_gest, "S0 L0 D0 LS0 SL0");
    lv_obj_align(s_label_gest, LV_ALIGN_BOTTOM_MID, 0, -46);

    /* 焦点初始给滑块 */
    lv_group_focus_obj(s_slider);
}

/* Menu/parent navigation follows the elec-cdemo UI structure. Each page
 * keeps its widgets alive; focus groups include only the visible page. */
/**
 * @brief 根据页面按钮的用户数据执行跳页。
 * @param e LVGL 点击事件，user_data 保存目标页编号。
 * @details 用法：page_button() 自动绑定；在 LVGL 线程执行。
 *          动作：取出页编号并调用 ui_show_page()，不销毁已有页面对象。
 */
static void page_event(lv_event_t *e)
{
    ui_show_page((unsigned)(uintptr_t)lv_event_get_user_data(e));
}
/**
 * @brief 创建一个带文字和跳页回调的按钮。
 * @param parent 父容器；text 为按钮文字；y 为距顶部偏移像素；page 为目标页编号。
 * @return 新建的按钮对象，生命周期由其父容器管理。
 * @details 用法：在 LVGL 线程建页面时调用；0=菜单、1=测量、2=诊断。
 *          动作：设置大小和位置，创建居中文字，把 page 绑定为点击事件的用户数据。
 */
static lv_obj_t *page_button(lv_obj_t *parent, const char *text, int y, unsigned page)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 196, 38);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, page_event, LV_EVENT_CLICKED, (void *)(uintptr_t)page);
    return button;
}
/**
 * @brief 创建统一背景、尺寸和标题的页面容器。
 * @param title 页面标题，LVGL 复制此字符串。
 * @return 新建的页面容器，挂在当前活动屏幕下。
 * @details 用法：在 LVGL 线程中建页时调用，返回后可继续添加子控件。
 *          动作：建立 240×320 容器，设置背景/文字颜色、关闭滚动，并添加顶部标题。
 */
static lv_obj_t *page_container(const char *title)
{
    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, 240, 320);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x111c2d), 0);
    lv_obj_set_style_text_color(page, lv_color_white(), 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    return page;
}
/**
 * @brief 切换可见页面，并重建按键可访问的控件组。
 * @param page 1=测量页，2=诊断页，其他值=菜单页。
 * @details 用法：页面均创建后在 LVGL 线程中调用；新增页面时同时添加其焦点控件。
 *          动作：清空 group 并退出编辑模式，隐藏全部页面，再显示目标页并加入该页控件。
 *          打开诊断页时读取 GP21 配置/统计并更新标签，这些诊断数字不是逐帧刷新的。
 */
static void ui_show_page(unsigned page)
{
    lv_group_t *group = lv_port_indev_get_group();
    lv_group_remove_all_objs(group);
    lv_group_set_editing(group, false);
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_monitor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_diag, LV_OBJ_FLAG_HIDDEN);
    if (page == 1) {
        lv_obj_clear_flag(s_monitor, LV_OBJ_FLAG_HIDDEN);
        lv_group_add_obj(group, s_slider);
        lv_group_add_obj(group, s_switch);
        lv_group_add_obj(group, s_btn);
        lv_group_add_obj(group, s_roller);
        lv_group_add_obj(group, s_back);
    } else if (page == 2) {
        lv_obj_clear_flag(s_diag, LV_OBJ_FLAG_HIDDEN);
        lv_group_add_obj(group, s_diag_back);
        gp21_stats_t st = {0};
        gp21_config_t cfg = {0};
        rt_device_t dev = rt_device_find(BSP_TDC_DEVICE_NAME);
        if (dev) {
            rt_device_control(dev, GP21_CTRL_GET_STATS, &st);
            rt_device_control(dev, GP21_CTRL_GET_CONFIG, &cfg);
        }
        lv_label_set_text_fmt(s_diag_label,
            "GP21 / ref %u kHz\nMCU clock %u MHz\nLCD 240 x 320 RGB565\n\nAcquisition: %s\nSamples: %u\nTimeouts: %u\nSPI errors: %u\nFIFO dropped: %u",
            (unsigned)(cfg.reference_hz / 1000u), (unsigned)(SystemCoreClock / 1000000u),
            st.running ? "running" : "offline / stopped", (unsigned)st.samples,
            (unsigned)st.timeouts, (unsigned)st.io_errors, (unsigned)st.dropped);
    } else {
        lv_obj_clear_flag(s_home, LV_OBJ_FLAG_HIDDEN);
        lv_group_add_obj(group, s_menu_buttons[0]);
        lv_group_add_obj(group, s_menu_buttons[1]);
    }
}
/**
 * @brief 创建主菜单、诊断页和返回按钮。
 * @details 用法：在 ui_create() 之后由 ui_init() 调用一次。
 *          动作：把测量页与诊断页接入菜单，添加按键帮助文字，最后显示菜单页。
 * @note 页面对象会保留；后续跳页只切换可见性和焦点组，不重复分配控件。
 */
static void ui_create_menu(void)
{
    s_back = page_button(s_monitor, "BACK", 278, 0);
    s_home = page_container("N32 / GP21");
    s_menu_buttons[0] = page_button(s_home, "Measurement / controls", 78, 1);
    s_menu_buttons[1] = page_button(s_home, "Device status", 132, 2);
    lv_obj_t *help = lv_label_create(s_home);
    lv_label_set_text(help, "PREV / NEXT: focus\nENTER: select / edit\nHold ENTER: leave edit\nDouble ENTER: menu");
    lv_obj_align(help, LV_ALIGN_TOP_LEFT, 16, 204);
    s_diag = page_container("Device status");
    s_diag_label = lv_label_create(s_diag);
    lv_obj_set_width(s_diag_label, 218);
    lv_obj_align(s_diag_label, LV_ALIGN_TOP_LEFT, 10, 60);
    s_diag_back = page_button(s_diag, "BACK", 278, 0);
    ui_show_page(0);
}

/**
 * @brief 初始化 LVGL、显示/输入移植层和应用页面。
 * @details 用法：由 lvgl_thread_entry() 在自己的线程中调用一次。
 *          动作：lv_init() 后打开显示设备；成功后注册输入、组合手势回调，创建界面并设置就绪标志。
 *          显示初始化失败则保留采集和串口功能；LVGL 时基由 LV_TICK_CUSTOM 读取 RT tick。
 */
static void ui_init(void)
{
    lv_init();

    if (lv_port_disp_init() != RT_EOK) {
        rt_kprintf("[ui] display unavailable; acquisition and shell remain active\n");
        return;
    }
    lv_port_indev_init();    /* board/lv_port_indev.c  3 按键 -> encoder + 手势 */

    /* 组合手势交给应用处理 */
    lv_port_indev_set_gesture_cb(app_gesture_cb);

    ui_create();
    ui_create_menu();
    s_ui_ready = 1;

    /* 心跳由 lv_conf.h 的 LV_TICK_CUSTOM 接 rt_tick_get(), 无需 lv_tick_inc */
}

/**
 * @brief 将采集结果快照更新到 LVGL 标签和样式。
 * @details 用法：由 app_ui_handler() 在 LVGL 线程周期调用，不从采集线程或中断直接调用。
 *          动作：先刷新变化的手势计数；未暂停时读取结果快照，应用显示倍率，更新数值/采样率并按阈值变色。
 * @note 界面未就绪或已切到官方演示时直接返回；读取快照会等待结果互斥锁。
 */
void app_ui_update(void)
{
    app_result_t r;
    double       shown;
    if (!s_ui_ready || s_demo_active) return;

    /* --- 手势计数: 只在变化时重刷, 避免每帧都重新分配标签文本 --- */
    if ((s_gest_dirty != 0u) && (s_label_gest != RT_NULL))
    {
        lv_label_set_text_fmt(s_label_gest, "S%u L%u D%u LS%u SL%u",
                              (unsigned)s_gest_cnt[KEY_GESTURE_SHORT],
                              (unsigned)s_gest_cnt[KEY_GESTURE_LONG],
                              (unsigned)s_gest_cnt[KEY_GESTURE_DOUBLE],
                              (unsigned)s_gest_cnt[KEY_GESTURE_LONG_SHORT],
                              (unsigned)s_gest_cnt[KEY_GESTURE_SHORT_LONG]);
        s_gest_dirty = 0u;
    }

    if (s_paused != 0u)
    {
        return;     /* "先短按接长按"切换成暂停: 冻住数值 */
    }

    app_result_get(&r);
    shown = r.value * s_scale_tab[s_scale_idx];

    if (s_label_value != RT_NULL)
    {
        /* %f 需要 LV_SPRINTF_USE_FLOAT=1 (board/lv_conf.h 已开) */
        lv_label_set_text_fmt(s_label_value, "%.3f us", shown);

        /* 超阈值标红, 顺便证明滑块/滚轮真的生效了 */
        lv_obj_set_style_text_color(s_label_value,
                                    (r.value > s_threshold) ? lv_color_hex(0xFF4040)
                                                            : lv_color_white(),
                                    0);
    }

    if (s_label_rate != RT_NULL)
    {
        lv_label_set_text_fmt(s_label_rate, "%u/s  ok %u  bad %u",
                              (unsigned)r.rate_hz,
                              (unsigned)r.count,
                              (unsigned)r.bad);
    }
}

/**
 * @brief 执行一轮界面任务，统一处理绘制与界面操作请求。
 * @details 用法：由 LVGL 线程约每 16 ms 调用；其他线程只设置请求，不直接调用这里。
 *          动作：取走色条测试请求；测试开启时暂停正常绘制，关闭时使整屏失效以重绘。
 *          再处理官方演示请求、更新应用数据，最后调用 lv_timer_handler() 完成输入、动画和刷屏。
 */
void app_ui_handler(void)
{
    if (!s_ui_ready) return;
    rt_base_t level = rt_hw_interrupt_disable();
    rt_uint8_t request = s_lcd_test_request;
    s_lcd_test_request = 0;
    rt_hw_interrupt_enable(level);
    if (request == 1) {
        rt_device_t lcd = rt_device_find("lcd0");
        s_lcd_test_active = 1;
        rt_device_control(lcd, LCD_CTRL_COLORBARS, RT_NULL);
    } else if (request == 2) {
        s_lcd_test_active = 0;
        lv_obj_invalidate(lv_scr_act());
    }
    if (s_lcd_test_active) return;
    if (s_demo_requested && !s_demo_active) {
        s_demo_requested = 0;
        s_demo_active = 1;
        lv_port_indev_set_gesture_cb(RT_NULL);
        lv_demo_keypad_encoder();
    }
    app_ui_update();
    lv_timer_handler();     /* 刷屏发生在里面(通过 flush_cb) */
}

/* --------------------------------------------------------------------------
 *  msh 命令: 打印 LVGL 状态, 验证图形库确实跑起来了
 *    在串口终端敲 lvgl 即可
 * -------------------------------------------------------------------------- */
#ifdef RT_USING_FINSH
/**
 * @brief 串口命令 lvgl：打印当前显示、输入和手势统计。
 * @param argc 参数数量；argv 参数字符串数组，本命令不解析附加参数。
 * @return 0 表示打印完成，界面未就绪返回 -RT_ERROR。
 * @details 用法：在 msh 输入 lvgl；由 shell 线程执行，只读诊断状态，不创建或修改控件。
 *          动作：读取版本、分辨率、缓冲行数、焦点组数量和手势计数；输出属于瞬时诊断快照。
 */
static int cmd_lvgl(int argc, char **argv)
{
    if (!s_ui_ready) { rt_kprintf("LVGL display is not ready\n"); return -RT_ERROR; }
    lv_disp_t *disp = lv_disp_get_default();
    lv_indev_t *indev = lv_port_indev_get();

    (void)argc;
    (void)argv;

    rt_kprintf("LVGL version : %d.%d.%d\n",
               LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    rt_kprintf("display      : %ux%u, buf %u lines\n",
               (unsigned)lv_disp_get_hor_res(disp),
               (unsigned)lv_disp_get_ver_res(disp),
               (unsigned)DISP_BUF_LINES);
    rt_kprintf("indev        : %s\n",
               (indev != RT_NULL) ? "encoder (3 keys)" : "none");
    rt_kprintf("group objs   : %u\n",
               (unsigned)lv_group_get_obj_count(lv_port_indev_get_group()));
    rt_kprintf("gestures     : S%u L%u D%u LS%u SL%u   last: %s\n",
               (unsigned)s_gest_cnt[KEY_GESTURE_SHORT],
               (unsigned)s_gest_cnt[KEY_GESTURE_LONG],
               (unsigned)s_gest_cnt[KEY_GESTURE_DOUBLE],
               (unsigned)s_gest_cnt[KEY_GESTURE_LONG_SHORT],
               (unsigned)s_gest_cnt[KEY_GESTURE_SHORT_LONG],
               key_gesture_name(s_gest_last));
    rt_kprintf("             : S=短按 L=长按 D=两次短按 LS=长按接短按 SL=短按接长按\n");
    rt_kprintf("ui thread    : %u ms period\n", (unsigned)UI_PERIOD_MS);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_lvgl, lvgl, show LVGL porting status);

/**
 * @brief 串口命令 lcd_test：请求显示色条或恢复应用界面。
 * @param argc 参数数量；argv 支持无参数、on 或 off。
 * @return RT_EOK 表示请求已提交；界面未就绪或参数不合法返回负错误码。
 * @details 用法：msh 输入 lcd_test on/off；不在 shell 中直接操作屏幕。
 *          动作：写入单个请求标志，由 LVGL 线程执行；连续快速提交时保留最新请求。
 */
static int cmd_lcd_test(int argc, char **argv)
{
    if (!s_ui_ready) return -RT_ERROR;
    if (argc == 1 || (argc == 2 && !rt_strcmp(argv[1], "on"))) s_lcd_test_request = 1;
    else if (argc == 2 && !rt_strcmp(argv[1], "off")) s_lcd_test_request = 2;
    else { rt_kprintf("lcd_test [on|off]\n"); return -RT_EINVAL; }
    rt_kprintf("[lcd] queued; lcd_test off restores UI\n");
    return RT_EOK;
}
MSH_CMD_EXPORT_ALIAS(cmd_lcd_test, lcd_test, show RGB white black bars or restore UI);

/**
 * @brief 串口命令 lvgl_demo：请求进入 LVGL 官方按键/编码器演示。
 * @param argc 参数数量；argv 参数数组，本命令不使用附加参数。
 * @return 0 表示已提交，界面未就绪返回 -RT_ERROR。
 * @details 用法：msh 输入 lvgl_demo，演示接管界面后需重启恢复应用页面。
 *          动作：只设置请求标志；真正创建演示控件及取消应用组合手势回调由 LVGL 线程完成。
 */
static int cmd_lvgl_demo(int argc, char **argv)
{
    if (!s_ui_ready) return -RT_ERROR;
    (void)argc;
    (void)argv;

    s_demo_requested = 1;
    rt_kprintf("[lvgl] demo queued; reboot to restore application\n");

    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_lvgl_demo, lvgl_demo, run LVGL keypad+encoder demo);
#endif /* RT_USING_FINSH */

#else  /* !APP_USE_LVGL */

/* --------------------------------------------------------------------------
 *  LVGL 未接入时的占位实现
 *
 *  保留完全相同的外部接口与线程节拍, 这样可以先单独验证:
 *      - 三个线程的优先级与抢占关系是否正确
 *      - 1kHz 采样会不会被显示线程拖慢（架构的关键验证点）
 * -------------------------------------------------------------------------- */
static rt_uint32_t s_ui_frames;

/**
 * @brief Headless 构建的界面初始化占位函数。
 * @details 用法：APP_USE_LVGL=0 时由界面线程调用。
 *          动作：不创建 LVGL 对象，也不初始化显示，保持应用启动接口一致。
 */
static void ui_init(void)
{
    /* 无图形界面时无需初始化 */
}

/**
 * @brief Headless 构建的界面更新占位函数。
 * @details 用法：仅在未启用 LVGL 的目标中编译。
 *          动作：空实现，不访问屏幕或修改采集结果。
 */
void app_ui_update(void)
{
    /* 无图形界面, 空实现 */
}

/**
 * @brief Headless 构建的周期任务占位函数。
 * @details 用法：由保留的界面线程周期调用。
 *          动作：只增加 s_ui_frames 计数，用于保留线程运行节拍，不执行绘制。
 */
void app_ui_handler(void)
{
    s_ui_frames++;
}

#endif /* APP_USE_LVGL */

/**
 * @brief 界面线程入口：初始化一次后按固定节拍处理界面。
 * @param parameter 线程启动参数，本工程未使用。
 * @details 用法：由 app_tasks_init() 创建；新增周期界面操作放入 app_ui_handler() 或其调用的函数。
 *          动作：调用 ui_init()，然后用 rt_thread_delay_until() 按 UI_PERIOD_MS 节拍等待并刷新。
 * @note 16 ms 是调度目标周期，实际帧率还受渲染耗时和 GPIO 刷屏耗时影响。
 */
static void lvgl_thread_entry(void *parameter)
{
    rt_tick_t next = rt_tick_get();

    (void)parameter;

    ui_init();

    for (;;)
    {
        rt_thread_delay_until(&next, rt_tick_from_millisecond(UI_PERIOD_MS));
        app_ui_handler();
    }
}

/* ==========================================================================
 *  结果读取
 * ========================================================================== */
/**
 * @brief 获取一份一致的测量统计快照，数值单位为微秒。
 * @param out 输出结构体地址；RT_NULL 时直接返回。
 * @details 用法：app_result_t r; app_result_get(&r); 在 app_tasks_init() 完成后的线程中调用。
 *          动作：获取结果互斥锁，复制 s_result 后立即释放锁；之后处理自己的副本即可。
 * @note 内部可能等待互斥锁，不可从中断或禁止调度的上下文调用。
 */
void app_result_get(app_result_t *out)
{
    if (out == RT_NULL)
    {
        return;
    }

    if (rt_mutex_take(s_result_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        *out = s_result;
        rt_mutex_release(s_result_lock);
    }
}

/**
 * @brief 把 GP21 驱动统计转换为应用层统计结构。
 * @param out 输出结构体地址；为空时忽略。
 * @details 用法：在线程中调用；设备不存在时输出为零的统计值。
 *          动作：通过 GET_STATS 取得驱动快照，将中断、记录、丢弃和错误计数映射到应用字段。
 * @note sample_bad 包含无效记录、通信错误和主机等待超时；bursts 表示完成的记录数。
 */
void app_tdc_stats_get(tdc_stats_t *out)
{
    gp21_stats_t st = {0};
    if (!out) return;
    rt_device_t dev = rt_device_find(BSP_TDC_DEVICE_NAME);
    if (dev) rt_device_control(dev, GP21_CTRL_GET_STATS, &st);
    out->int_count = st.interrupts;
    out->bursts = st.samples;
    out->coalesced = st.dropped;
    out->sample_ok = st.samples - st.invalid;
    out->sample_bad = st.invalid + st.io_errors + st.timeouts;
}

/* ==========================================================================
 *  初始化
 * ========================================================================== */
/**
 * @brief 创建采集处理线程和 LVGL/Headless 周期线程。
 * @return RT_EOK 表示完成；线程初始化失败时返回对应错误码。
 * @details 用法：已由 INIT_APP_EXPORT 注册，系统启动自动执行一次，main() 无需再次调用。
 *          动作：初始化结果互斥锁与采样信号量，绑定静态线程栈，再启动 proc 和 lvgl 线程。
 * @note 优先级和栈大小在 inc/app_tasks.h 修改；此函数不是可重复调用的重启接口。
 */
int app_tasks_init(void)
{
    rt_mutex_init(s_result_lock, "result", RT_IPC_FLAG_PRIO);
    rt_sem_init(&s_ready, "sample", 0, RT_IPC_FLAG_PRIO);
    rt_err_t err = rt_thread_init(&s_proc_thread, "proc", proc_thread_entry, RT_NULL,
        s_proc_stack, sizeof(s_proc_stack), APP_PRIO_PROC, 10);
    if (err != RT_EOK) return err;
    err = rt_thread_init(&s_lvgl_thread, "lvgl", lvgl_thread_entry, RT_NULL,
        s_lvgl_stack, sizeof(s_lvgl_stack), APP_PRIO_LVGL, 10);
    if (err != RT_EOK) return err;
    rt_thread_startup(&s_proc_thread);
    rt_thread_startup(&s_lvgl_thread);
    return RT_EOK;
}
INIT_APP_EXPORT(app_tasks_init);

/* ==========================================================================
 *  msh 命令: 打印采集统计, 排查"没有数据"时第一个要看的东西
 * ========================================================================== */
#ifdef RT_USING_FINSH
/**
 * @brief 串口命令 tdc：查看 GP21 统计或控制采集。
 * @param argc 参数数量；argv 支持 start、stop、selftest，无参数时打印状态。
 * @return 0 或设备控制结果；设备缺失、参数不合法等情况返回负错误码。
 * @details 用法：msh 输入 tdc；通信自检前先输入 tdc stop，再输入 tdc selftest。
 *          动作：通过 rt_device_control() 转发控制命令，或读取计数并打印外部参考时钟/脉冲接线提示。
 */
static int cmd_tdc(int argc, char **argv)
{
    rt_device_t dev = rt_device_find(BSP_TDC_DEVICE_NAME);
    if (!dev) return -RT_ENOSYS;
    if (argc > 1) {
        int cmd = !rt_strcmp(argv[1], "start") ? GP21_CTRL_START :
                  !rt_strcmp(argv[1], "stop") ? GP21_CTRL_STOP :
                  !rt_strcmp(argv[1], "selftest") ? GP21_CTRL_SELFTEST : -1;
        if (cmd < 0) { rt_kprintf("tdc [start|stop|selftest]\n"); return -RT_EINVAL; }
        rt_err_t err = rt_device_control(dev, cmd, RT_NULL);
        rt_kprintf("tdc %s: %d (selftest requires stop)\n", argv[1], (int)err);
        return err;
    }
    gp21_stats_t st;
    rt_device_control(dev, GP21_CTRL_GET_STATS, &st);
    rt_kprintf("GP21 %u Hz, INT pin=%d, running=%d\n", BSP_TDC_REF_HZ,
               BSP_TDC_INT_PIN, st.running);
    rt_kprintf("irq=%u samples=%u invalid=%u timeout=%u io=%u dropped=%u\n",
        (unsigned)st.interrupts, (unsigned)st.samples, (unsigned)st.invalid,
        (unsigned)st.timeouts, (unsigned)st.io_errors, (unsigned)st.dropped);
    rt_kprintf("Enable START/STOP1, supply external 5MHz reference, then pulses.\n");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_tdc, tdc, show TDC acquisition statistics);
#endif /* RT_USING_FINSH */
