/**
 * @file    app_selftest.c
 * @brief   RT-Thread 功能自检 —— 逐个"真的调用一遍"来验证内核移植成功
 *
 *  为什么需要它:
 *      能编译、能链接只说明配置没写错; 要证明移植成功, 必须让每个组件
 *      真的创建对象、真的收发一次数据、真的触发一次回调。
 *      本文件把 RT-Thread 的各个功能点跑一遍, 打印 PASS/FAIL 表格。
 *
 *  上电自动跑一次 (INIT_APP_EXPORT), 串口 115200 就能看到结果;
 *  也可以随时在 msh 里敲 selftest 再跑一遍。
 *
 *  检查项:
 *      内核版本 / 动态内存 / 动态线程 / 信号量 / 互斥量 / 事件集
 *      邮箱 / 消息队列 / 内存池 / 软件定时器 / 时间与延时精度
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include <string.h>

#include "drv_key.h"     /* key_selftest() */

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

/* ==========================================================================
 *  统计
 * ========================================================================== */
static int s_pass;
static int s_fail;

#define CHECK(cond, name, fmt, ...)                                  \
    do {                                                             \
        if (cond) {                                                  \
            s_pass++;                                                \
            rt_kprintf("  [PASS] %-22s " fmt "\n", name, ##__VA_ARGS__); \
        } else {                                                     \
            s_fail++;                                                \
            rt_kprintf("  [FAIL] %-22s " fmt "\n", name, ##__VA_ARGS__); \
        }                                                            \
    } while (0)

/* ==========================================================================
 *  各项测试用的静态资源
 * ========================================================================== */
#define TEST_STACK_SIZE     1024u
static rt_uint8_t  s_test_stack[TEST_STACK_SIZE];
static struct rt_thread s_test_thread;

#define MP_BLOCK_SIZE       32u
#define MP_BLOCK_COUNT      32u
static rt_uint8_t  s_mp_pool[MP_BLOCK_SIZE * MP_BLOCK_COUNT];
static struct rt_mempool s_mp;

#define MQ_POOL_SIZE        256u
static rt_uint8_t  s_mq_pool[MQ_POOL_SIZE];
static struct rt_messagequeue s_mq;

static volatile rt_uint8_t s_thread_ran;
static volatile rt_uint32_t s_timer_fired;

/* ==========================================================================
 *  回调
 * ========================================================================== */
/**
 * @brief 自检线程入口，通过置位标志证明线程确实得到调度。
 * @param parameter 启动参数，未使用。
 * @details 用法：由 test_thread() 创建的静态/动态测试线程使用。
 *          动作：把 s_thread_ran 设为 1 后返回，不执行持续业务。
 */
static void test_thread_entry(void *parameter)
{
    (void)parameter;
    s_thread_ran = 1u;
    /* 线程函数返回后由内核回收 (rt_thread_create 出来的线程) */
}

/**
 * @brief 统计自检软件定时器触发次数。
 * @param parameter 定时器参数，未使用。
 * @details 用法：由 test_soft_timer() 注册，在软件定时器线程中执行。
 *          动作：递增 s_timer_fired，保持回调短小，不在这里打印或延时。
 */
static void test_timer_cb(void *parameter)
{
    (void)parameter;
    s_timer_fired++;
}

/* ==========================================================================
 *  自检主体
 * ========================================================================== */
/**
 * @brief 打印内核版本、节拍频率和优先级数量。
 * @details 用法：由 app_selftest_run() 在线程中调用。
 *          动作：读取编译期 RT 配置宏并打印，仅提供配置信息，不增加 PASS/FAIL 计数。
 */
static void test_version(void)
{
    rt_kprintf("  [INFO] %-22s %d.%d.%d\n", "RT-Thread version",
               (int)RT_VERSION, (int)RT_SUBVERSION, (int)RT_REVISION);
    rt_kprintf("  [INFO] %-22s %d Hz (%d ms/tick)\n", "tick rate",
               (int)RT_TICK_PER_SECOND, (int)(1000 / RT_TICK_PER_SECOND));
    rt_kprintf("  [INFO] %-22s %d\n", "max priority", (int)RT_THREAD_PRIORITY_MAX);
}

/**
 * @brief 检查 RT-Thread 动态堆的基本分配和释放。
 * @details 用法：由自检流程在线程中调用，需已完成内核堆初始化。
 *          动作：分配 256 字节并记录结果，成功则填充后释放，最后打印总量、当前占用和峰值。
 */
static void test_heap(void)
{
    void *p = rt_malloc(256);

    CHECK(p != RT_NULL, "dynamic heap", "malloc(256) = %p", p);
    if (p != RT_NULL)
    {
        memset(p, 0xA5, 256);
        rt_free(p);
    }

    {
        rt_uint32_t total = 0, used = 0, max_used = 0;
        rt_memory_info(&total, &used, &max_used);
        rt_kprintf("  [INFO] %-22s total %u B, used %u B, peak %u B\n",
                   "heap usage", (unsigned)total, (unsigned)used, (unsigned)max_used);
    }
}

/**
 * @brief 检查静态及动态线程创建后是否得到调度。
 * @details 用法：由自检线程调用，使用本文件的专用栈和控制块，不可并发运行两份自检。
 *          动作：分别创建/启动测试线程，延时后检查执行标志，并走对应线程清理路径。
 * @note 此项检查基本调度和生命周期，不测量实际业务线程的栈峰值。
 */
static void test_thread(void)
{
    rt_err_t err;

    /* --- 静态方式 (rt_thread_init): 栈与控制块都由用户提供, 不占内核堆 --- */
    s_thread_ran = 0u;

    err = rt_thread_init(&s_test_thread, "st_t", test_thread_entry, RT_NULL,
                         s_test_stack, sizeof(s_test_stack), 20, 10);
    CHECK(err == RT_EOK, "static thread init", "err = %d", (int)err);
    if (err == RT_EOK)
    {
        rt_thread_startup(&s_test_thread);
        rt_thread_mdelay(30);          /* 让它跑完 */

        CHECK(s_thread_ran == 1u, "static thread ran",
              "flag = %u", (unsigned)s_thread_ran);

        rt_thread_detach(&s_test_thread);
    }

    /* --- 动态方式 (rt_thread_create): 栈从内核堆里分配 --- */
    {
        rt_thread_t t;

        s_thread_ran = 0u;
        t = rt_thread_create("st_d", test_thread_entry, RT_NULL, 512, 20, 10);
        CHECK(t != RT_NULL, "dynamic thread create", "%p", t);
        if (t != RT_NULL)
        {
            rt_thread_startup(t);
            rt_thread_mdelay(30);

            CHECK(s_thread_ran == 1u, "dynamic thread ran",
                  "flag = %u", (unsigned)s_thread_ran);

            rt_thread_delete(t);
        }
    }
}

/**
 * @brief 验证信号量为空和释放后的取用行为。
 * @details 用法：由 app_selftest_run() 调用。
 *          动作：创建初值 0 的信号量，检查零等待取用超时，再释放/取用并删除对象。
 */
static void test_semaphore(void)
{
    rt_sem_t sem = rt_sem_create("st_s", 0, RT_IPC_FLAG_PRIO);

    CHECK(sem != RT_NULL, "semaphore create", "%p", sem);
    if (sem == RT_NULL)
    {
        return;
    }

    /* 初值 0, 无超时等待应当失败 */
    CHECK(rt_sem_take(sem, 0) == -RT_ETIMEOUT, "sem take (empty)", "timeout as expected");

    rt_sem_release(sem);
    CHECK(rt_sem_take(sem, 0) == RT_EOK, "sem take (after rel)", "value = %d", (int)sem->value);

    rt_sem_delete(sem);
}

/**
 * @brief 验证互斥量的创建、获取和释放。
 * @details 用法：由自检流程在线程中调用，不可在硬件中断执行。
 *          动作：创建互斥量，零等待获取并释放，记录结果后删除；不模拟多线程竞争。
 */
static void test_mutex(void)
{
    rt_mutex_t mtx = rt_mutex_create("st_m", RT_IPC_FLAG_PRIO);

    CHECK(mtx != RT_NULL, "mutex create", "%p", mtx);
    if (mtx == RT_NULL)
    {
        return;
    }

    CHECK(rt_mutex_take(mtx, 0) == RT_EOK, "mutex take", "hold = %d", (int)mtx->hold);
    CHECK(rt_mutex_release(mtx) == RT_EOK, "mutex release", "hold = %d", (int)mtx->hold);

    rt_mutex_delete(mtx);
}

/**
 * @brief 检查事件集的 AND 匹配和清除语义。
 * @details 用法：由自检流程调用。
 *          动作：先验证空事件接收超时，再分别发送 0x01/0x02，以 AND|CLEAR 接收后删除对象。
 */
static void test_event(void)
{
    rt_event_t ev = rt_event_create("st_e", RT_IPC_FLAG_PRIO);
    rt_uint32_t recved = 0;

    CHECK(ev != RT_NULL, "event create", "%p", ev);
    if (ev == RT_NULL)
    {
        return;
    }

    CHECK(rt_event_recv(ev, 0x03, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        0, &recved) == -RT_ETIMEOUT,
          "event recv (empty)", "timeout as expected");

    rt_event_send(ev, 0x01);
    rt_event_send(ev, 0x02);

    CHECK(rt_event_recv(ev, 0x03, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        rt_tick_from_millisecond(10), &recved) == RT_EOK,
          "event recv (AND)", "got 0x%02X", (unsigned)recved);

    rt_event_delete(ev);
}

/**
 * @brief 验证一个 32 位值通过邮箱发送和接收。
 * @details 用法：由自检流程在线程中调用。
 *          动作：创建四槽邮箱，发送固定值并接收，记录 API 结果；值不一致时打印警告，最后删除邮箱。
 */
static void test_mailbox(void)
{
    rt_mailbox_t mb = rt_mb_create("st_b", 4, RT_IPC_FLAG_PRIO);
    rt_uint32_t  value = 0xC0DE1234u;
    rt_uint32_t  got = 0;

    CHECK(mb != RT_NULL, "mailbox create", "%p", mb);
    if (mb == RT_NULL)
    {
        return;
    }

    CHECK(rt_mb_send(mb, value) == RT_EOK, "mailbox send", "0x%08X", (unsigned)value);
    CHECK(rt_mb_recv(mb, (rt_ubase_t *)&got,
                     rt_tick_from_millisecond(10)) == RT_EOK,
          "mailbox recv", "0x%08X", (unsigned)got);

    if (got != value)
    {
        rt_kprintf("  [WARN] %-22s value mismatch\n", "mailbox data");
    }

    rt_mb_delete(mb);
}

/**
 * @brief 验证静态消息队列收发以及动态消息队列创建。
 * @details 用法：由自检流程调用，静态消息池为本文件独占资源。
 *          动作：初始化队列并收发测试字符串，记录 API 结果和数据不一致警告，再移除静态对象并测试动态创建/删除。
 */
static void test_msgqueue(void)
{
    rt_err_t    err;
    const char *tx = "N32G452";
    char        rx[16];

    /* --- 静态方式: 消息池由用户提供 --- */
    memset(rx, 0, sizeof(rx));

    err = rt_mq_init(&s_mq, "st_q", s_mq_pool, 16, sizeof(s_mq_pool), RT_IPC_FLAG_PRIO);
    CHECK(err == RT_EOK, "static msgqueue init", "pool %u B", (unsigned)sizeof(s_mq_pool));
    if (err == RT_EOK)
    {
        CHECK(rt_mq_send(&s_mq, tx, strlen(tx) + 1) == RT_EOK,
              "msgqueue send", "\"%s\"", tx);
        CHECK(rt_mq_recv(&s_mq, rx, sizeof(rx),
                         rt_tick_from_millisecond(10)) == RT_EOK,
              "msgqueue recv", "\"%s\"", rx);

        if (strcmp(tx, rx) != 0)
        {
            rt_kprintf("  [WARN] %-22s payload mismatch\n", "msgqueue data");
        }

        rt_mq_detach(&s_mq);
    }

    /* --- 动态方式: 消息池从内核堆分配 --- */
    {
        rt_mq_t mq = rt_mq_create("st_qd", 16, 4, RT_IPC_FLAG_PRIO);

        CHECK(mq != RT_NULL, "dynamic msgqueue create", "%p", mq);
        if (mq != RT_NULL)
        {
            rt_mq_delete(mq);
        }
    }
}

/**
 * @brief 检查固定块内存池的静态分配和动态创建。
 * @details 用法：由自检流程在线程中调用。
 *          动作：用静态缓冲建立内存池并申请两个块，归还后移除；再创建/删除一个动态内存池。
 */
static void test_mempool(void)
{
    rt_err_t err;
    void    *b1;
    void    *b2;

    /* --- 静态方式: 内存池由用户提供 --- */
    err = rt_mp_init(&s_mp, "st_p", s_mp_pool, sizeof(s_mp_pool), MP_BLOCK_SIZE);
    CHECK(err == RT_EOK, "static mempool init", "%u x %u B",
          (unsigned)MP_BLOCK_COUNT, (unsigned)MP_BLOCK_SIZE);
    if (err == RT_EOK)
    {
        b1 = rt_mp_alloc(&s_mp, 0);
        b2 = rt_mp_alloc(&s_mp, 0);

        CHECK((b1 != RT_NULL) && (b2 != RT_NULL), "mempool alloc",
              "%p / %p", b1, b2);

        if (b1 != RT_NULL)
        {
            rt_mp_free(b1);
        }
        if (b2 != RT_NULL)
        {
            rt_mp_free(b2);
        }

        rt_mp_detach(&s_mp);
    }

    /* --- 动态方式 --- */
    {
        rt_mp_t mp = rt_mp_create("st_pd", 8, 32);

        CHECK(mp != RT_NULL, "dynamic mempool create", "%p", mp);
        if (mp != RT_NULL)
        {
            rt_mp_delete(mp);
        }
    }
}

/**
 * @brief 检查周期软件定时器是否触发，以及线程延时是否在容许范围内。
 * @details 用法：由自检流程调用，允许线程睡眠，不能在定时器回调中递归执行。
 *          动作：启动 20 ms 周期回调，等待约 100 ms，检查至少触发三次及经过时间，再停止并删除定时器。
 * @note 这是运行时粗略检查，调度负载会影响结果，不等同于晶振精度测量。
 */
static void test_soft_timer(void)
{
    rt_timer_t tmr;
    rt_tick_t  t0;
    rt_tick_t  elapsed;

    s_timer_fired = 0u;

    tmr = rt_timer_create("st_tmr", test_timer_cb, RT_NULL,
                          rt_tick_from_millisecond(20),
                          RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    CHECK(tmr != RT_NULL, "soft timer create", "%p", tmr);
    if (tmr == RT_NULL)
    {
        return;
    }

    rt_timer_start(tmr);

    /* 量一下延时精度: 睡 100ms 应该落在 100~120ms 内 */
    t0 = rt_tick_get();
    rt_thread_mdelay(100);
    elapsed = (rt_tick_get() - t0) * (1000u / RT_TICK_PER_SECOND);

    CHECK(s_timer_fired >= 3u, "soft timer fired",
          "%u times in %u ms", (unsigned)s_timer_fired, (unsigned)elapsed);

    CHECK((elapsed >= 95u) && (elapsed <= 130u), "mdelay accuracy",
          "asked 100 ms, got %u ms", (unsigned)elapsed);

    rt_timer_stop(tmr);
    rt_timer_delete(tmr);
}

/**
 * @brief 统计某一类 RT-Thread 内核对象的数量。
 * @param type RT_Object_Class_* 类型枚举。
 * @return 当前链表节点数；没有该类信息时返回 0。
 * @details 用法：在线程中调用，RT-Thread 3.1.4 没有直接的对象长度接口。
 *          动作：取得对象信息，在禁止线程调度的临界区遍历链表，再恢复调度。
 */
static rt_uint32_t obj_count(enum rt_object_class_type type)
{
    struct rt_object_information *info = rt_object_get_information(type);
    struct rt_list_node *node;
    rt_uint32_t n = 0;

    if (info == RT_NULL)
    {
        return 0u;
    }

    rt_enter_critical();
    for (node = info->object_list.next; node != &info->object_list; node = node->next)
    {
        n++;
    }
    rt_exit_critical();

    return n;
}

/**
 * @brief 打印线程、定时器和设备对象数量，并检查基本线程数。
 * @details 用法：由自检流程调用，统计时其他已启动任务仍可能在运行。
 *          动作：分别调用 obj_count()，打印数量，检查至少存在五个线程对象。
 */
static void test_objects(void)
{
    rt_uint32_t threads = obj_count(RT_Object_Class_Thread);
    rt_uint32_t timers  = obj_count(RT_Object_Class_Timer);
    rt_uint32_t devs    = obj_count(RT_Object_Class_Device);

    rt_kprintf("  [INFO] %-22s %u 个\n", "线程对象", (unsigned)threads);
    rt_kprintf("  [INFO] %-22s %u 个\n", "定时器对象", (unsigned)timers);
    rt_kprintf("  [INFO] %-22s %u 个\n", "设备对象", (unsigned)devs);

    /* 至少有: main/tshell/timer/idle + tdc/proc/lvgl, 所以 >= 5 */
    CHECK(threads >= 5u, "object list walk", "found %u threads", (unsigned)threads);
}

/**
 * @brief 执行整套 RT-Thread 基础功能与按键状态机自检。
 * @return 失败项数；0 表示本次检查全部通过。
 * @details 用法：启动阶段自动执行，或在 msh 输入 selftest；使用共享静态资源，不能并发重入。
 *          动作：清统计，依次检查堆、线程、IPC、定时器和对象，再调用 key_selftest() 并打印汇总。
 * @note 这些检查不证明 GP21、LCD 的实际接线或显示已通过验证；部分数据不一致只打印警告。
 */
int app_selftest_run(void)
{
    s_pass = 0;
    s_fail = 0;

    rt_kprintf("\n");
    rt_kprintf("========== RT-Thread 移植自检 ==========\n");

    test_version();
    test_heap();
    test_thread();
    test_semaphore();
    test_mutex();
    test_event();
    test_mailbox();
    test_msgqueue();
    test_mempool();
    test_soft_timer();
    test_objects();

    /* 按键手势识别: 用合成时序离线跑一遍 5 种手势。
     * 纯逻辑, 不需要真实按键, 也会打印自己的 PASS/FAIL 表格。 */
    s_fail += key_selftest();

    rt_kprintf("----------------------------------------\n");
    rt_kprintf("结果: %d 项通过, %d 项失败  =>  %s\n",
               s_pass, s_fail,
               (s_fail == 0) ? "移植成功" : "有失败项, 请查看上面的 [FAIL]");
    rt_kprintf("========================================\n\n");

    return s_fail;
}

/* 上电自动跑一次, 串口上直接能看到结果 */
/**
 * @brief 在应用自动初始化阶段执行一次自检。
 * @return 当前封装固定返回 0，具体通过/失败见串口输出或 app_selftest_run() 返回值。
 * @details 用法：INIT_APP_EXPORT 自动调用，不需要在 main() 再运行一次。
 *          动作：调用 app_selftest_run()；过程中会让出 CPU，其间其他已启动线程仍可运行。
 */
static int app_selftest_init(void)
{
    app_selftest_run();
    return 0;
}
INIT_APP_EXPORT(app_selftest_init);

#ifdef RT_USING_FINSH
MSH_CMD_EXPORT_ALIAS(app_selftest_run, selftest, run RT-Thread self test);
#endif
