/**
 * @file    key_gesture_test.c
 * @brief   按键手势识别器的主机端测试入口 —— 在 PC 上跑, 不用板子
 *
 *  用例集在 ../board/key_gesture_selftest.h, 与板子上 drv_key.c 的
 *  key_selftest() 共用同一份, 所以两边结果一定一致。
 *
 *  编译运行 (msys64 / MinGW / 任意主机 gcc):
 *      gcc -std=c99 -Wall -Wextra -I ../board key_gesture_test.c ../board/key_gesture.c -o key_gesture_test
 *      ./key_gesture_test              # 用默认参数 600 / 300
 *      ./key_gesture_test 800 400      # 试试别的长按阈值 / 组合窗口
 *
 *  为什么值得这么做:
 *      手势识别是典型的时序逻辑, 边界条件(差一个扫描周期、窗口刚好内外)
 *      最容易出错。在 PC 上跑几十个用例, 比在板子上人肉按几十次按键
 *      可靠得多, 也快得多。这也正是把 key_gesture.c 写成"零依赖纯逻辑"
 *      的原因 —— 能这么测, 是设计出来的, 不是碰巧。
 *
 *  ⚠️ 退出码 0 表示全部通过, 可以直接接进 CI。
 */

#include <stdio.h>
#include <stdlib.h>

/* 先告诉测试用例往哪打印, 再包含它 */
#define KEY_GESTURE_TEST_PRINT(...)     printf(__VA_ARGS__)

#include "key_gesture_selftest.h"

#define DEF_LONG_MS     600
#define DEF_SEQ_MS      300
#define DEF_SCAN_MS     10

int main(int argc, char **argv)
{
    unsigned short long_ms = DEF_LONG_MS;
    unsigned short seq_ms  = DEF_SEQ_MS;

    if (argc > 1)
    {
        long_ms = (unsigned short)atoi(argv[1]);
    }
    if (argc > 2)
    {
        seq_ms = (unsigned short)atoi(argv[2]);
    }

    if (seq_ms == 0)
    {
        printf("注意: 组合窗口为 0 时不支持组合手势 "
               "(PREV/NEXT 就是这种配置), 下面的组合用例会失败, 属正常。\n");
    }

    return (key_gesture_test_run(long_ms, seq_ms, DEF_SCAN_MS) == 0) ? 0 : 1;
}
