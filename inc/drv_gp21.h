#ifndef DRV_GP21_H
#define DRV_GP21_H
#include <rtdevice.h>
#include "gp21_protocol.h"

enum {
    GP21_CTRL_GET_CONFIG = 0x100,
    GP21_CTRL_SET_CONFIG,
    GP21_CTRL_SELFTEST,
    GP21_CTRL_START,
    GP21_CTRL_STOP,
    GP21_CTRL_GET_STATS
};
typedef struct {
    uint32_t reference_hz;
    uint32_t regs[7];
} gp21_config_t;
typedef struct {
    rt_uint32_t sequence, tick, raw;
    rt_uint64_t time_ps;
    rt_uint16_t status;
    rt_uint8_t valid;
} gp21_sample_t;
typedef struct {
    rt_uint32_t interrupts, samples, invalid, timeouts, io_errors, dropped;
    rt_bool_t running;
} gp21_stats_t;
/**
 * @brief GP21 设备使用顺序及控制命令约定。
 * @details 设备名默认 tdc0，启动后由 app_tasks.c 的 proc 线程独占打开。
 *          其他线程读取统计请用 app_tdc_stats_get()，读取处理结果用 app_result_get()。
 *          自行接管采集时，应先移除原 proc 消费者，再按 find -> open -> START -> read 使用。
 *          rt_device_read(dev, 0, &sample, sizeof(sample)) 返回的是字节数；
 *          返回完整结构后检查 valid，再使用 time_ps（皮秒）或 raw（16.16 定点原始值）。
 *          无新记录返回 0；read 会消费 FIFO，多个消费者不会各获得一份完整数据。
 *          rx_indicate 在 GP21 工作线程中调用，适合释放信号量唤醒消费者。
 *
 * 控制命令及 arg：
 * - GET_CONFIG：gp21_config_t* 输出；reference_hz 是外部参考频率，不是 SPI 时钟。
 * - SET_CONFIG：const gp21_config_t* 输入，先 STOP；仅支持本驱动的数字量程 2 配置。
 * - GET_STATS：gp21_stats_t* 输出；samples 包含无效记录，dropped 表示 FIFO 覆盖的旧记录。
 * - SELFTEST：RT_NULL，要求已 open 且已 STOP，只检查通信回读。
 * - START/STOP：RT_NULL；STOP 取消当前等待但不清空已入队记录。
 * @note 设备接口会获取互斥锁，只能在线程中使用；中断回调仅通知工作线程。
 */
#endif
