#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "board_config.h"
#include "drv_gp21.h"

#define GP21_FIFO_SIZE 16u
static struct {
    struct rt_device dev;
    struct rt_spi_device *spi;
    struct rt_mutex lock;
    struct rt_semaphore irq;
    struct rt_semaphore wake;
    struct rt_thread worker;
    ALIGN(RT_ALIGN_SIZE) rt_uint8_t stack[2048];
    gp21_config_t config;
    gp21_stats_t stats;
    gp21_sample_t fifo[GP21_FIFO_SIZE];
    rt_uint32_t head, tail, sequence, generation;
    rt_bool_t opened;
} tdc;

/**
 * @brief 向 GP21 发送一个单字节操作码。
 * @param cmd GP21_RESET、GP21_INIT 等操作码。
 * @return RT_EOK=发送成功，-RT_EIO=未完整发送。
 * @details 用法：内部线程路径调用，先完成 SPI 设备绑定，并按上层约定持有 tdc.lock。
 *          动作：交给 rt_spi_send() 完成片选与传输，命令本身不读取响应。
 */
static rt_err_t command(rt_uint8_t cmd)
{
    return rt_spi_send(tdc.spi, &cmd, 1) == 1 ? RT_EOK : -RT_EIO;
}
/**
 * @brief 在一次 SPI 命令/接收事务中读取 GP21 寄存器。
 * @param addr 寄存器地址；data 为接收缓冲；bytes 为接收字节数，调用者保证空间足够。
 * @return rt_spi_send_then_recv() 的结果，RT_EOK 表示成功。
 * @details 用法：内部在设备互斥锁保护下调用。
 *          动作：发送 0xB0|addr 后保持事务连续读取数据；状态和结果的解码由上层完成。
 */
static rt_err_t read_register(rt_uint8_t addr, void *data, rt_size_t bytes)
{
    rt_uint8_t cmd = 0xb0u | addr;
    return rt_spi_send_then_recv(tdc.spi, &cmd, 1, data, bytes);
}
/**
 * @brief 写入 GP21 的一个 32 位配置寄存器。
 * @param addr 0..6 的地址；value 为包含低位标识字节的完整 32 位值。
 * @return RT_EOK=成功，-RT_EINVAL=地址无效，-RT_EIO=传输不完整。
 * @details 用法：配置芯片时在设备锁内调用。
 *          动作：用 gp21_write_frame() 生成操作码和高字节在前的四字节数据，再发送整个五字节帧。
 */
static rt_err_t write_register(rt_uint8_t addr, rt_uint32_t value)
{
    rt_uint8_t frame[5];
    if (gp21_write_frame(addr, value, frame)) return -RT_EINVAL;
    return rt_spi_send(tdc.spi, frame, sizeof(frame)) == sizeof(frame) ? RT_EOK : -RT_EIO;
}
/**
 * @brief 回读配置标识字节，检查 GP21 通信和寄存器写入结果。
 * @return RT_EOK=回读一致，-RT_EIO=读取失败或内容不匹配。
 * @details 用法：配置后或停止测量后的 SELFTEST 中调用，需持有设备锁。
 *          动作：读取七个寄存器低字节标识及寄存器 1 高字节，与当前配置比较。
 * @note 这是通信回读检查，不是读取唯一芯片序列号，也不验证时间测量精度。
 */
static rt_err_t check_id(void)
{
    rt_uint8_t id[7], high;
    if (read_register(7, id, sizeof(id)) != RT_EOK ||
        read_register(5, &high, 1) != RT_EOK) return -RT_EIO;
    for (unsigned i = 0; i < 7; ++i)
        if (id[i] != (rt_uint8_t)tdc.config.regs[i]) return -RT_EIO;
    return high == (rt_uint8_t)(tdc.config.regs[1] >> 24) ? RT_EOK : -RT_EIO;
}
/**
 * @brief 复位 GP21、写入当前配置并执行通信回读检查。
 * @return RT_EOK=成功，其他值表示 SPI 发送或回读失败。
 * @details 用法：打开设备或停止后修改配置时在 tdc.lock 内调用，必须处于可延时的线程上下文。
 *          动作：发送 RESET、等待 2 ms，依次写寄存器 0..6，最后调用 check_id()。
 */
static rt_err_t configure_chip(void)
{
    if (command(GP21_RESET) != RT_EOK) return -RT_EIO;
    rt_thread_mdelay(2);
    for (unsigned i = 0; i < 7; ++i)
        if (write_register(i, tdc.config.regs[i]) != RT_EOK) return -RT_EIO;
    return check_id();
}
/**
 * @brief GP21 INTN 下降沿回调，只记录中断并通知工作线程。
 * @param arg 用户参数，本工程未使用。
 * @details 用法：注册到 PIN 中断框架，由 EXTI 中断调用，应用不要直接调用。
 *          动作：增加中断计数并释放 irq 信号量；SPI 读取由 worker() 完成。
 * @note 中断中不能等待 SPI 总线互斥锁，也不能延时或操作 LVGL。
 */
static void irq_handler(void *arg)
{
    (void)arg;
    tdc.stats.interrupts++;
    rt_sem_release(&tdc.irq);
}
/**
 * @brief 禁止 GP21 引脚中断并清空已经积累的中断通知。
 * @details 用法：内部停止、重启、关闭或错误恢复路径在设备锁内调用。
 *          动作：关闭 INTN 对应 EXTI，再以零等待反复取走 irq 信号量，防止旧通知进入下一次测量。
 */
static void irq_stop(void)
{
    rt_pin_irq_enable(BSP_TDC_INT_PIN, PIN_IRQ_DISABLE);
    while (rt_sem_take(&tdc.irq, 0) == RT_EOK) {}
}
/* Called with the device mutex held. A generation invalidates waits after
 * STOP/START/close, so a cancelled wait cannot publish an old conversion. */
/**
 * @brief 处理一次测量等待结果，将真实完成记录放入 FIFO，并重新准备下一次测量。
 * @param wait 中断等待结果；generation 为等待开始时保存的采集代次。
 * @return RT_TRUE=已入队一条记录、需要通知应用；RT_FALSE=没有新记录。
 * @details 用法：只能由工作线程在持有 tdc.lock 时调用。
 *          动作：先确认仍在运行且代次一致；等待超时且 INTN 为高时只记超时，不重复发布旧结果。
 *          否则读取状态和 RES0，解码/验证并换算皮秒；FIFO 满时丢最旧记录，随后发送 INIT 重新布防。
 * @note STOP/START/close 会改变代次，使已取消的等待不能发布过期测量；INIT 失败会停止采集。
 */
static rt_bool_t acquire_once(rt_err_t wait, rt_uint32_t generation)
{
    rt_bool_t notify = RT_FALSE;
    if (tdc.stats.running && generation == tdc.generation) {
        rt_uint8_t status[2], raw[4];
        gp21_sample_t sample = {0};
        /* A timeout with INTN high means no complete conversion: do not
         * publish stale RES0. Rearm so an incomplete conversion recovers. */
        if (wait != RT_EOK && rt_pin_read(BSP_TDC_INT_PIN) != PIN_LOW) {
            tdc.stats.timeouts++;
        } else if (read_register(4, status, 2) != RT_EOK ||
                   read_register(0, raw, 4) != RT_EOK) {
            tdc.stats.io_errors++;
        } else {
            sample.sequence = ++tdc.sequence;
            sample.tick = rt_tick_get();
            sample.status = ((rt_uint16_t)status[0] << 8) | status[1];
            sample.raw = gp21_decode_u32(raw);
            sample.valid = gp21_result_valid(sample.status, sample.raw);
            unsigned divcode = (tdc.config.regs[0] >> 20) & 3u;
            sample.time_ps = sample.valid ? gp21_result_ps(sample.raw,
                tdc.config.reference_hz, divcode == 0 ? 1 : divcode == 1 ? 2 : 4) : 0;
            tdc.stats.samples++;
            if (!sample.valid) tdc.stats.invalid++;
            if (tdc.head - tdc.tail == GP21_FIFO_SIZE) {
                tdc.tail++;
                tdc.stats.dropped++;
            }
            tdc.fifo[tdc.head++ % GP21_FIFO_SIZE] = sample;
            notify = RT_TRUE;
        }
        while (rt_sem_take(&tdc.irq, 0) == RT_EOK) {}
        if (command(GP21_INIT) != RT_EOK) {
            tdc.stats.io_errors++;
            tdc.stats.running = RT_FALSE;
            irq_stop();
        }
    }
    return notify;
}
/**
 * @brief GP21 驱动工作线程，负责可阻塞的 SPI 读取和数据通知。
 * @param arg 线程参数，未使用。
 * @details 用法：由 gp21_register() 创建一次，应用通过设备控制命令启动/停止采集。
 *          动作：停机时等待 wake；运行时在锁外等待 INTN 或超时，再加锁调用 acquire_once()。
 *          记录入队后释放设备锁，再调用 rx_indicate，避免通知回调中读设备发生锁重入。
 */
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        rt_bool_t notify = RT_FALSE;
        rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
        if (!tdc.stats.running) {
            rt_mutex_release(&tdc.lock);
            rt_sem_take(&tdc.wake, RT_WAITING_FOREVER);
            continue;
        }
        rt_uint32_t generation = tdc.generation;
        rt_mutex_release(&tdc.lock);
        rt_err_t wait = rt_pin_read(BSP_TDC_INT_PIN) == PIN_LOW ? RT_EOK :
            rt_sem_take(&tdc.irq, rt_tick_from_millisecond(BSP_TDC_WAIT_MS));
        rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
        notify = acquire_once(wait, generation);
        rt_mutex_release(&tdc.lock);
        if (notify && tdc.dev.rx_indicate) tdc.dev.rx_indicate(&tdc.dev, sizeof(gp21_sample_t));
    }
}
/**
 * @brief RT-Thread 设备初始化回调，暂不探测硬件。
 * @param dev GP21 设备对象。
 * @return 始终返回 RT_EOK。
 * @details 用法：由设备框架调用；实际探测放在 open，便于未接芯片时在后续打开操作中重试。
 *          动作：不发送 SPI、不启动测量。
 */
static rt_err_t device_init(rt_device_t dev)
{
    (void)dev;
    return RT_EOK; /* Probe in open so absent hardware can be retried. */
}
/**
 * @brief 打开 GP21 设备并验证芯片配置通信。
 * @param dev 设备对象；flags 为 RT_DEVICE_OFLAG_* 打开标志。
 * @return RT_EOK=成功，负值表示芯片配置或回读失败。
 * @details 用法：应用在线程中调用 rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY)，设备只允许独占打开。
 *          动作：在设备锁内复位并配置芯片，成功后置 opened、清空记录队列并保存打开标志。
 * @note 打开成功后还需 GP21_CTRL_START；当前 proc 线程会自动占用设备，其他任务不要再次打开。
 */
static rt_err_t device_open(rt_device_t dev, rt_uint16_t flags)
{
    (void)dev; (void)flags;
    rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
    rt_err_t err = configure_chip();
    if (err == RT_EOK) {
        tdc.opened = RT_TRUE;
        tdc.head = tdc.tail = 0;
        dev->open_flag = flags & RT_DEVICE_OFLAG_MASK;
    }
    rt_mutex_release(&tdc.lock);
    return err;
}
/**
 * @brief 关闭 GP21：取消等待、停止中断并移除通知回调。
 * @param dev 要关闭的 GP21 设备对象。
 * @return RT_EOK；复位命令的返回值在此路径中被忽略。
 * @details 用法：拥有设备的线程调用 rt_device_close()。
 *          动作：加锁停止运行并递增代次，清中断通知、发送 RESET、清 opened/rx_indicate，最后唤醒旧等待。
 */
static rt_err_t device_close(rt_device_t dev)
{
    rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
    tdc.stats.running = RT_FALSE;
    tdc.generation++;
    irq_stop();
    (void)command(GP21_RESET);
    tdc.opened = RT_FALSE;
    dev->rx_indicate = RT_NULL;
    rt_mutex_release(&tdc.lock);
    rt_sem_release(&tdc.irq);
    return RT_EOK;
}
/**
 * @brief 非阻塞地从 GP21 FIFO 取出已经完成的记录。
 * @param dev 设备对象；pos 必须为 0；buffer 为 gp21_sample_t 数组；size 为缓冲容量字节数。
 * @return 实际复制的字节数，一定是 sizeof(gp21_sample_t) 的整数倍；空队列或无效参数返回 0。
 * @details 用法：rt_device_read(dev, 0, &sample, sizeof(sample))；返回完整记录后仍须检查 sample.valid。
 *          动作：加锁按 FIFO 顺序复制完整记录并推进队尾，不等待新测量、不触发硬件采样。
 * @note 非阻塞指不等待新记录；互斥锁仍可能使线程等待，因此不能从中断调用。
 */
static rt_size_t device_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    (void)dev;
    if (!buffer || pos != 0 || size < sizeof(gp21_sample_t)) return 0;
    rt_size_t count = 0;
    rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
    while (tdc.tail != tdc.head && size >= sizeof(gp21_sample_t)) {
        ((gp21_sample_t *)buffer)[count++] = tdc.fifo[tdc.tail++ % GP21_FIFO_SIZE];
        size -= sizeof(gp21_sample_t);
    }
    rt_mutex_release(&tdc.lock);
    return count * sizeof(gp21_sample_t);
}
/**
 * @brief 实现 GP21 配置、统计、自检及采集启停命令。
 * @param dev 设备对象；cmd 为 GP21_CTRL_*；arg 按命令传配置/统计地址或 RT_NULL。
 * @return RT_EOK=成功；EINVAL=参数不支持，EBUSY=需先停机，EIO=未打开/通信失败，ENOSYS=未知命令，均为负值。
 * @details 用法：在线程中调用 rt_device_control()；SET_CONFIG 传 gp21_config_t*，GET_STATS 传 gp21_stats_t*。
 *          动作：所有命令均在设备锁内执行；改配置/自检要求停机，配置写入失败时恢复旧配置并尝试重新写回。
 *          START 准备新一轮测量并唤醒 worker；STOP 改变代次并唤醒旧等待；GET 命令复制当前快照。
 * @note 本接口仅接受当前单路数字量程 2 数据语义可表达的配置；STOP 不清 FIFO，重新 open 才会清空。
 */
static rt_err_t device_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    rt_err_t err = RT_EOK;
    rt_mutex_take(&tdc.lock, RT_WAITING_FOREVER);
    switch (cmd) {
    case GP21_CTRL_GET_CONFIG:
        if (arg) *(gp21_config_t *)arg = tdc.config; else err = -RT_EINVAL;
        break;
    case GP21_CTRL_GET_STATS:
        if (arg) {
            rt_base_t level = rt_hw_interrupt_disable();
            *(gp21_stats_t *)arg = tdc.stats;
            rt_hw_interrupt_enable(level);
        } else err = -RT_EINVAL;
        break;
    case GP21_CTRL_SET_CONFIG: {
        gp21_config_t *c = arg;
        if (!c) { err = -RT_EINVAL; break; }
        if (tdc.stats.running) { err = -RT_EBUSY; break; }
        /* This device publishes a single calibrated digital range-2 interval.
         * Reject profiles whose semantics cannot be represented by read(). */
        unsigned divcode = (c->regs[0] >> 20) & 3u;
        unsigned div = divcode == 0 ? 1 : divcode == 1 ? 2 : 4;
        if (c->reference_hz / div < 2000000u || c->reference_hz / div > 8000000u ||
            (c->regs[0] & ~0x003007ffu) != (0x02066800u & ~0x003007ffu) ||
            (c->regs[1] & 0xffffff00u) != 0x21020000u ||
            (c->regs[2] & 0xffffff00u) != 0xa0000000u ||
            (c->regs[3] & 0xffffff00u) != 0x38000000u ||
            (c->regs[4] & 0xffffff00u) != 0x20000000u ||
            (c->regs[5] & 0xffffff00u) != 0 || (c->regs[6] & 0xffffff00u) != 0) {
            err = -RT_EINVAL; break;
        }
        gp21_config_t old = tdc.config;
        tdc.config = *c;
        err = tdc.opened ? configure_chip() : RT_EOK;
        if (err != RT_EOK) { tdc.config = old; (void)configure_chip(); }
        break;
    }
    case GP21_CTRL_SELFTEST:
        err = !tdc.opened ? -RT_EIO : tdc.stats.running ? -RT_EBUSY : check_id();
        break;
    case GP21_CTRL_START:
        if (!tdc.opened) { err = -RT_EIO; break; }
        if (tdc.stats.running) break;
        tdc.generation++;
        irq_stop();
        err = command(GP21_INIT);
        if (err == RT_EOK) err = rt_pin_irq_enable(BSP_TDC_INT_PIN, PIN_IRQ_ENABLE);
        tdc.stats.running = err == RT_EOK;
        /* Wake idle worker without fabricating a data-ready notification. */
        rt_sem_release(&tdc.wake);
        break;
    case GP21_CTRL_STOP:
        tdc.stats.running = RT_FALSE;
        tdc.generation++;
        irq_stop();
        if (tdc.opened) (void)command(GP21_INIT);
        rt_sem_release(&tdc.irq);
        break;
    default: err = -RT_ENOSYS; break;
    }
    rt_mutex_release(&tdc.lock);
    return err;
}
/**
 * @brief 把 GP21 注册为 tdc0 传感器设备并创建工作线程。
 * @return RT_EOK=成功；依赖缺失、SPI 配置、中断绑定、设备/线程注册失败返回对应错误。
 * @details 用法：由 INIT_COMPONENT_EXPORT 自动执行一次，在 SPI 从设备挂载之后、应用线程启动之前运行。
 *          动作：配置 SPI Mode 1/8 位，加载外部参考时钟和默认寄存器，创建锁/信号量，配置复位与使能脚。
 *          随后绑定 INTN 下降沿、注册只读独占设备并启动优先级 4 的工作线程；初始不启动采集。
 * @note 改接线和 5 MHz 参考值在 board_config.h；SPI 传输频率与 GP21 外部参考频率是两个参数。
 */
static int gp21_register(void)
{
    tdc.spi = (struct rt_spi_device *)rt_device_find(BSP_TDC_SPI_NAME);
    if (!tdc.spi) return -RT_ENOSYS;
    struct rt_spi_configuration cfg = {0};
    cfg.mode = RT_SPI_MODE_1 | RT_SPI_MSB;
    cfg.data_width = 8;
    cfg.max_hz = BSP_TDC_SPI_HZ;
    rt_err_t err = rt_spi_configure(tdc.spi, &cfg);
    if (err != RT_EOK) return err;
    tdc.config.reference_hz = BSP_TDC_REF_HZ;
    gp21_default_config(tdc.config.regs);
    rt_mutex_init(&tdc.lock, "gp21", RT_IPC_FLAG_PRIO);
    rt_sem_init(&tdc.irq, "gpirq", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&tdc.wake, "gpwake", 0, RT_IPC_FLAG_PRIO);
    if (BSP_TDC_RESET_PIN >= 0) {
        rt_pin_write(BSP_TDC_RESET_PIN, PIN_HIGH);
        rt_pin_mode(BSP_TDC_RESET_PIN, PIN_MODE_OUTPUT);
        rt_pin_write(BSP_TDC_RESET_PIN, PIN_LOW);
        rt_thread_mdelay(2);
        rt_pin_write(BSP_TDC_RESET_PIN, PIN_HIGH);
        rt_thread_mdelay(2);
    }
    const rt_base_t enables[] = {BSP_TDC_EN_START_PIN, BSP_TDC_EN_STOP1_PIN};
    for (unsigned i = 0; i < 2; ++i) if (enables[i] >= 0) {
        rt_pin_write(enables[i], PIN_HIGH);
        rt_pin_mode(enables[i], PIN_MODE_OUTPUT);
    }
    rt_pin_mode(BSP_TDC_INT_PIN, PIN_MODE_INPUT_PULLUP);
    err = rt_pin_attach_irq(BSP_TDC_INT_PIN, PIN_IRQ_MODE_FALLING, irq_handler, RT_NULL);
    if (err != RT_EOK) return err;
    tdc.dev.type = RT_Device_Class_Sensor;
    tdc.dev.init = device_init;
    tdc.dev.open = device_open;
    tdc.dev.close = device_close;
    tdc.dev.read = device_read;
    tdc.dev.control = device_control;
    err = rt_device_register(&tdc.dev, BSP_TDC_DEVICE_NAME,
                            RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_STANDALONE);
    if (err != RT_EOK) return err;
    err = rt_thread_init(&tdc.worker, "gp21", worker, RT_NULL,
                        tdc.stack, sizeof(tdc.stack), 4, 5);
    if (err != RT_EOK) return err;
    return rt_thread_startup(&tdc.worker);
}
INIT_COMPONENT_EXPORT(gp21_register);
