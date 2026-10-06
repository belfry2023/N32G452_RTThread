/** RT-Thread application: consume tdc0 records and render a button-driven UI.
 * GP21 driver worker priority 4, proc priority 10, LVGL priority 16.
 * One completed hardware conversion produces one record (no burst rereading).
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define APP_PRIO_PROC 10
#define APP_PRIO_LVGL 16
#define APP_STACK_PROC 2048u
#define APP_STACK_LVGL 4096u
/* Legacy field names kept for application compatibility. */
typedef struct {
    uint32_t int_count;
    uint32_t bursts;     /* completed conversions */
    uint32_t coalesced;  /* FIFO records dropped */
    uint32_t sample_ok;
    uint32_t sample_bad; /* invalid records + I/O errors + host timeouts */
} tdc_stats_t;
typedef struct {
    uint32_t count, bad, bursts; /* bursts = consumed records */
    double value, value_min, value_max; /* microseconds, before display scaling */
    uint32_t rate_hz;
} app_result_t;
/**
 * @brief 创建采集处理线程和 LVGL/Headless 周期线程。
 * @return RT_EOK 表示完成；线程初始化失败时返回对应错误码。
 * @details 用法：已由 INIT_APP_EXPORT 注册，系统启动自动执行一次，main() 无需再次调用。
 *          动作：初始化结果互斥锁与采样信号量，绑定静态线程栈，再启动 proc 和 lvgl 线程。
 * @note 优先级和栈大小在 inc/app_tasks.h 修改；此函数不是可重复调用的重启接口。
 */
int app_tasks_init(void); /* automatic INIT_APP_EXPORT */
/**
 * @brief 获取一份一致的测量统计快照，数值单位为微秒。
 * @param out 输出结构体地址；RT_NULL 时直接返回。
 * @details 用法：app_result_t r; app_result_get(&r); 在 app_tasks_init() 完成后的线程中调用。
 *          动作：获取结果互斥锁，复制 s_result 后立即释放锁；之后处理自己的副本即可。
 * @note 内部可能等待互斥锁，不可从中断或禁止调度的上下文调用。
 */
void app_result_get(app_result_t *out); /* thread-safe snapshot */
/**
 * @brief 把 GP21 驱动统计转换为应用层统计结构。
 * @param out 输出结构体地址；为空时忽略。
 * @details 用法：在线程中调用；设备不存在时输出为零的统计值。
 *          动作：通过 GET_STATS 取得驱动快照，将中断、记录、丢弃和错误计数映射到应用字段。
 * @note sample_bad 包含无效记录、通信错误和主机等待超时；bursts 表示完成的记录数。
 */
void app_tdc_stats_get(tdc_stats_t *out);
/**
 * @brief 将采集结果快照更新到 LVGL 标签和样式。
 * @details 用法：由 app_ui_handler() 在 LVGL 线程周期调用，不从采集线程或中断直接调用。
 *          动作：先刷新变化的手势计数；未暂停时读取结果快照，应用显示倍率，更新数值/采样率并按阈值变色。
 * @note 界面未就绪或已切到官方演示时直接返回；读取快照会等待结果互斥锁。
 */
void app_ui_update(void);  /* LVGL thread only */
/**
 * @brief 执行一轮界面任务，统一处理绘制与界面操作请求。
 * @details 用法：由 LVGL 线程约每 16 ms 调用；其他线程只设置请求，不直接调用这里。
 *          动作：取走色条测试请求；测试开启时暂停正常绘制，关闭时使整屏失效以重绘。
 *          再处理官方演示请求、更新应用数据，最后调用 lv_timer_handler() 完成输入、动画和刷屏。
 */
void app_ui_handler(void); /* LVGL thread only */
#ifdef __cplusplus
}
#endif
#endif
