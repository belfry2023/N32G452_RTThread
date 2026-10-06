/**
 * @file    key_gesture_selftest.h
 * @brief   按键手势识别的测试用例集 —— 板子与 PC 共用同一份
 *
 *  用法（调用方先定义打印宏, 再包含本文件）:
 *
 *      板子上 (board/drv_key.c):
 *          #define KEY_GESTURE_TEST_PRINT(...)  rt_kprintf(__VA_ARGS__)
 *          #include "key_gesture_selftest.h"
 *
 *      PC 上 (tools/key_gesture_test.c):
 *          #define KEY_GESTURE_TEST_PRINT(...)  printf(__VA_ARGS__)
 *          #include "key_gesture_selftest.h"
 *
 *  为什么要共用:
 *      如果板子和 PC 各写一份用例, 早晚会写歪 —— 一边过了另一边没过,
 *      却不知道信谁。共用一份就只有一个真相。
 *
 *  为什么要能在 PC 上跑:
 *      key_gesture.c 是纯逻辑(只吃"电平 + 毫秒时间戳"), 合成了一个时序
 *      喂进去就能验证。手势识别这种时序逻辑, 在 PC 上跑几十个用例
 *      比在板子上按几十次按键可靠得多, 也不用人肉掐秒表。
 *
 *  ⚠️ 边界用例的数值是【按 long_ms / seq_ms 算出来的】, 不是写死的,
 *     所以改了 board/drv_key.c 顶部的参数, 这套用例照样成立。
 */
#ifndef __KEY_GESTURE_SELFTEST_H__
#define __KEY_GESTURE_SELFTEST_H__

#include "key_gesture.h"

#ifndef KEY_GESTURE_TEST_PRINT
#error "包含本文件前必须先定义 KEY_GESTURE_TEST_PRINT(fmt, ...)"
#endif

/* ==========================================================================
 *  一段输入: 电平保持多久
 * ========================================================================== */
typedef struct
{
    unsigned char  pressed;     /* 1 = 按下, 0 = 松开 */
    unsigned short ms;          /* 这一段持续多少毫秒 */
} key_test_step_t;

/* ==========================================================================
 *  跑一个场景
 * ========================================================================== */
/**
 * @brief 按指定扫描节拍向识别器输入一个场景，并比较输出手势。
 * @param name 用例名；steps/step_cnt 为电平及持续时间序列；expect/expect_cnt 为预期事件序列。
 * @param trailing_release 非 0 表示收尾松开，0 表示持续按住以检查假事件。
 * @param long_ms 长按阈值；seq_ms 组合窗口；scan_ms 为非零扫描周期，单位均为毫秒。
 * @return 0=符合预期，1=事件数量或顺序不符。
 * @details 用法：由 key_gesture_test_run() 传入合法数组和时间参数，支持 PC 与板端共用。
 *          动作：创建独立识别器，按节拍模拟每段电平及收尾观察期，取出队列并逐项比较、打印结果。
 */
static int key_test_case(const char *name,
                         const key_test_step_t *steps, int step_cnt,
                         const key_gesture_t *expect, int expect_cnt,
                         int trailing_release,
                         unsigned short long_ms, unsigned short seq_ms,
                         unsigned short scan_ms)
{
    key_gesture_ctx_t ctx;
    key_gesture_cfg_t cfg;
    key_gesture_t     got[8];
    int               got_cnt = 0;
    unsigned int      t = 0;
    int               i;
    int               ok;

    cfg.long_ms = long_ms;
    cfg.seq_ms  = seq_ms;
    key_gesture_init(&ctx, &cfg);

    /* 按 scan_ms 节拍喂入合成时序 */
    for (i = 0; i < step_cnt; i++)
    {
        unsigned int elapsed = 0;
        while (elapsed < steps[i].ms)
        {
            key_gesture_step(&ctx, steps[i].pressed, t);
            t += scan_ms;
            elapsed += scan_ms;
        }
    }

    /* 收尾: 空跑 1.5 个窗口, 让挂在等待窗口里的手势落地 */
    for (i = 0; i < (int)(((unsigned int)seq_ms * 3u / 2u) / scan_ms); i++)
    {
        key_gesture_step(&ctx, (trailing_release != 0) ? 0u : 1u, t);
        t += scan_ms;
    }

    while (got_cnt < 8)
    {
        key_gesture_t g = key_gesture_take(&ctx);
        if (g == KEY_GESTURE_NONE)
        {
            break;
        }
        got[got_cnt++] = g;
    }

    ok = (got_cnt == expect_cnt) ? 1 : 0;
    for (i = 0; ok && (i < expect_cnt); i++)
    {
        if (got[i] != expect[i])
        {
            ok = 0;
        }
    }

    KEY_GESTURE_TEST_PRINT("  [%s] %-24s ->", ok ? "PASS" : "FAIL", name);
    for (i = 0; i < got_cnt; i++)
    {
        KEY_GESTURE_TEST_PRINT(" %s", key_gesture_name(got[i]));
    }
    if (!ok)
    {
        KEY_GESTURE_TEST_PRINT("   期望:");
        for (i = 0; i < expect_cnt; i++)
        {
            KEY_GESTURE_TEST_PRINT(" %s", key_gesture_name(expect[i]));
        }
    }
    KEY_GESTURE_TEST_PRINT("\n");

    return ok ? 0 : 1;
}

/* ==========================================================================
 *  全部用例
 * ========================================================================== */
#define KEY_TEST_CASES  17

/**
 * @brief 执行共享的 17 项按键手势逻辑测试。
 * @param long_ms 长按阈值；seq_ms 组合窗口；scan_ms 为非零扫描周期，均为毫秒。
 * @return 失败用例总数，0 表示全部通过。
 * @details 用法：key_selftest() 和 PC 测试程序调用；参数应采用被测配置，边界时长由这些参数推导。
 *          动作：构造短按、长按、组合、边界和持续按住等场景，逐项调用 key_test_case() 并累计失败数。
 */
static int key_gesture_test_run(unsigned short long_ms, unsigned short seq_ms,
                                unsigned short scan_ms)
{
    int fail = 0;

    /* 各项时长按参数推导, 不写死 */
    const unsigned short short_ms = 80;
    const unsigned short mid_ms   = 100;                       /* 两下之间的间隔 */
    const unsigned short lo_ms    = (unsigned short)(long_ms - scan_ms);  /* 差一点 */
    const unsigned short hi_ms    = (unsigned short)(long_ms + scan_ms);  /* 过一点 */
    const unsigned short win_ms   = (unsigned short)(seq_ms * 2 / 3);     /* 窗口内 */
    const unsigned short out_ms   = (unsigned short)(seq_ms + seq_ms / 3);/* 窗口外 */
    const unsigned short tail_ms  = 700;

    key_test_step_t s[8];

    KEY_GESTURE_TEST_PRINT("\n========== 按键手势识别自检 ==========\n");
    KEY_GESTURE_TEST_PRINT("参数: 长按阈值 %ums, 组合窗口 %ums, 扫描 %ums\n",
                           long_ms, seq_ms, scan_ms);

    /* ---------------- 5 种基本手势 ---------------- */
    KEY_GESTURE_TEST_PRINT("-- 5 种手势 --\n");
    {
        const key_gesture_t e[] = { KEY_GESTURE_SHORT };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = tail_ms;
        fail += key_test_case("短按", s, 2, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG };
        s[0].pressed = 1; s[0].ms = hi_ms;
        s[1].pressed = 0; s[1].ms = tail_ms;
        fail += key_test_case("长按", s, 2, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_DOUBLE };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("两次短按", s, 4, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG_SHORT };
        s[0].pressed = 1; s[0].ms = hi_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("先长按接短按", s, 4, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_SHORT_LONG };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = hi_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("先短按接长按", s, 4, e, 1, 1, long_ms, seq_ms, scan_ms);
    }

    /* ---------------- 长按阈值边界 ---------------- */
    KEY_GESTURE_TEST_PRINT("-- 长按阈值边界 (%ums) --\n", long_ms);
    {
        const key_gesture_t e[] = { KEY_GESTURE_SHORT };
        s[0].pressed = 1; s[0].ms = lo_ms;
        s[1].pressed = 0; s[1].ms = tail_ms;
        fail += key_test_case("差一个扫描周期", s, 2, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG };
        s[0].pressed = 1; s[0].ms = long_ms;
        s[1].pressed = 0; s[1].ms = tail_ms;
        fail += key_test_case("刚好到阈值", s, 2, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG };
        s[0].pressed = 1; s[0].ms = hi_ms;
        s[1].pressed = 0; s[1].ms = tail_ms;
        fail += key_test_case("刚过阈值", s, 2, e, 1, 1, long_ms, seq_ms, scan_ms);
    }

    /* ---------------- 组合窗口边界 ---------------- */
    KEY_GESTURE_TEST_PRINT("-- 组合窗口边界 (%ums) --\n", seq_ms);
    {
        const key_gesture_t e[] = { KEY_GESTURE_DOUBLE };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = win_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("间隔在窗口内", s, 4, e, 1, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_SHORT, KEY_GESTURE_SHORT };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = out_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("间隔超出窗口", s, 4, e, 2, 1, long_ms, seq_ms, scan_ms);
    }

    /* ---------------- 多次连击 ---------------- */
    KEY_GESTURE_TEST_PRINT("-- 多次连击 --\n");
    {
        const key_gesture_t e[] = { KEY_GESTURE_DOUBLE, KEY_GESTURE_SHORT };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = mid_ms;
        s[4].pressed = 1; s[4].ms = short_ms;
        s[5].pressed = 0; s[5].ms = tail_ms;
        fail += key_test_case("三次短按", s, 6, e, 2, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_DOUBLE, KEY_GESTURE_DOUBLE };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = short_ms;
        s[3].pressed = 0; s[3].ms = out_ms;
        s[4].pressed = 1; s[4].ms = short_ms;
        s[5].pressed = 0; s[5].ms = mid_ms;
        s[6].pressed = 1; s[6].ms = short_ms;
        s[7].pressed = 0; s[7].ms = tail_ms;
        fail += key_test_case("双击+间隔+双击", s, 8, e, 2, 1, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG, KEY_GESTURE_LONG };
        s[0].pressed = 1; s[0].ms = hi_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = hi_ms;
        s[3].pressed = 0; s[3].ms = tail_ms;
        fail += key_test_case("长按接长按", s, 4, e, 2, 1, long_ms, seq_ms, scan_ms);
    }

    /* ---------------- ★ 回归: 按住不放时不能漏出假手势 ----------------
     * "先短按接长按"是在【按住过程中】到达阈值就判定完成的(反馈更及时),
     * 如果没有 SM_LOCK 状态, 状态机回到 IDLE 又会把同一次按住当成新按下,
     * 松手时就会凭空多出一个短按。 */
    KEY_GESTURE_TEST_PRINT("-- 回归: 按住不放 --\n");
    {
        s[0].pressed = 1; s[0].ms = 2000;
        fail += key_test_case("一直按住不松手", s, 1, 0, 0, 0, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_SHORT_LONG };
        s[0].pressed = 1; s[0].ms = short_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = 2000;
        fail += key_test_case("短接长(按住不放)", s, 3, e, 1, 0, long_ms, seq_ms, scan_ms);
    }
    {
        const key_gesture_t e[] = { KEY_GESTURE_LONG, KEY_GESTURE_LONG };
        s[0].pressed = 1; s[0].ms = hi_ms;
        s[1].pressed = 0; s[1].ms = mid_ms;
        s[2].pressed = 1; s[2].ms = 2000;
        fail += key_test_case("长接长(按住不放)", s, 3, e, 2, 0, long_ms, seq_ms, scan_ms);
    }
    {
        s[0].pressed = 0; s[0].ms = 1000;
        fail += key_test_case("完全无操作", s, 1, 0, 0, 1, long_ms, seq_ms, scan_ms);
    }

    KEY_GESTURE_TEST_PRINT("--------------------------------------\n");
    KEY_GESTURE_TEST_PRINT("结果: %d / %d 项失败  =>  %s\n",
                           fail, KEY_TEST_CASES,
                           (fail == 0) ? "全部通过" : "有失败");
    KEY_GESTURE_TEST_PRINT("======================================\n\n");

    return fail;
}

#endif /* __KEY_GESTURE_SELFTEST_H__ */
