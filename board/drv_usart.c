/**
 * @file    drv_usart.c
 * @brief   RT-Thread 串口设备驱动 (USART1) —— 为 msh/FINSH 提供输入通道
 *
 *  为什么需要这个文件:
 *      board.c 里的 rt_hw_console_output() 只解决了"往外打印",
 *      而 FINSH/msh 是交互式 shell, 还需要"从串口收字符"。
 *      收字符必须走 RT-Thread 的设备框架:
 *          串口中断 -> rt_hw_serial_isr(RX_IND) -> 环形缓冲 -> finsh 线程 read
 *      所以这里实现一个完整的 rt_serial 设备。
 *
 *  ⚠️ rt_console_set_device() 内部只用 RT_DEVICE_FLAG_STREAM 打开设备,
 *     不会申请 RX 缓冲。真正让 RX 生效的是 FINSH 自己
 *     用 RT_DEVICE_FLAG_INT_RX 再 open 一次 (见 shell.c 的 finsh_set_device)。
 *     board.c 里也显式开了一次 INT_RX, 保证不依赖 FINSH 配置。
 *
 *  参考: SDK projects/n32g45x_EVAL/examples/RT_Thread/DeviceDrivers/uart/src/drv_usart.c
 */

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>

#include "n32g45x.h"
#include "board_config.h"
#include "n32g45x_gpio.h"
#include "n32g45x_rcc.h"
#include "n32g45x_usart.h"

#ifdef RT_USING_SERIAL

#ifndef RT_USING_USART1
#error "drv_usart.c 需要开启 RT_USING_USART1"
#endif

/* ==========================================================================
 *  硬件描述   ← 换串口只改这里
 * ========================================================================== */
#define UART_PERIPH         USART1
#define UART_IRQN           USART1_IRQn
#define UART_PERIPH_CLK     RCC_APB2_PERIPH_USART1
#define UART_GPIO_CLK BSP_GPIO_CLOCKS
#define UART_TX_PORT BSP_CONSOLE_PORT
#define UART_TX_PIN BSP_CONSOLE_TX
#define UART_RX_PORT BSP_CONSOLE_PORT
#define UART_RX_PIN BSP_CONSOLE_RX
#define UART_DEVICE_NAME    "usart1"

/* ==========================================================================
 *  设备对象
 * ========================================================================== */
static struct rt_serial_device s_serial1;
static struct rt_serial_device *const s_uart = &s_serial1;

/* ==========================================================================
 *  硬件初始化 (引脚 + 时钟 + NVIC)
 * ========================================================================== */
/**
 * @brief 初始化 USART1 的引脚、时钟和中断向量。
 * @param cfg 串口配置，使用其中 mode 决定是否配置 RX 引脚。
 * @details 用法：由串口 configure 回调调用；不在应用中重复配置同一外设。
 *          动作：启用 GPIO/USART1 时钟，设置 TX 复用输出和可选 RX 输入，再配置 NVIC 分组和使能。
 * @note NVIC 分组影响整颗芯片；修改该步骤需同时核对其他中断优先级。
 */
static void uart_hw_init(struct serial_configure *cfg)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(UART_GPIO_CLK | UART_PERIPH_CLK | RCC_APB2_PERIPH_AFIO,
                            ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* TX: 复用推挽输出 */
    gpio.Pin       = UART_TX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitPeripheral(UART_TX_PORT, &gpio);

    /* RX: 浮空输入 */
    if ((cfg->mode == RX_MODE) || (cfg->mode == TX_RX_MODE))
    {
        gpio.Pin       = UART_RX_PIN;
        gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_InitPeripheral(UART_RX_PORT, &gpio);
    }

    /* 只用抢占优先级, 不用子优先级 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
    NVIC_SetPriority(UART_IRQN, 1);
    NVIC_EnableIRQ(UART_IRQN);
}

/* ==========================================================================
 *  rt_serial_ops
 * ========================================================================== */
/**
 * @brief 将 RT-Thread 串口参数写入 USART1。
 * @param serial 非空串口对象；cfg 为波特率、字长、停止位、校验和收发模式配置。
 * @return RT_EOK；当前实现对未识别的部分枚举值使用默认分支。
 * @details 用法：应用通过 rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &cfg) 调用。
 *          动作：初始化硬件引脚，映射各配置字段，关闭硬件流控并使能 USART1。
 */
static rt_err_t n32_uart_configure(struct rt_serial_device *serial,
                                   struct serial_configure *cfg)
{
    USART_InitType init;

    RT_ASSERT(serial != RT_NULL);
    RT_ASSERT(cfg != RT_NULL);

    uart_hw_init(cfg);

    USART_StructInit(&init);
    init.BaudRate = cfg->baud_rate;

    switch (cfg->data_bits)
    {
        case DATA_BITS_9:
            init.WordLength = USART_WL_9B;
            break;
        default:
            init.WordLength = USART_WL_8B;
            break;
    }

    switch (cfg->stop_bits)
    {
        case STOP_BITS_2:
            init.StopBits = USART_STPB_0_5;
            break;
        case STOP_BITS_3:
            init.StopBits = USART_STPB_2;
            break;
        case STOP_BITS_4:
            init.StopBits = USART_STPB_1_5;
            break;
        case STOP_BITS_1:
        default:
            init.StopBits = USART_STPB_1;
            break;
    }

    switch (cfg->parity)
    {
        case PARITY_ODD:
            init.Parity = USART_PE_ODD;
            break;
        case PARITY_EVEN:
            init.Parity = USART_PE_EVEN;
            break;
        case PARITY_NONE:
        default:
            init.Parity = USART_PE_NO;
            break;
    }

    init.HardwareFlowControl = USART_HFCTRL_NONE;

    switch (cfg->mode)
    {
        case TX_MODE:
            init.Mode = USART_MODE_TX;
            break;
        case RX_MODE:
            init.Mode = USART_MODE_RX;
            break;
        case TX_RX_MODE:
        default:
            init.Mode = USART_MODE_TX | USART_MODE_RX;
            break;
    }

    USART_Init(UART_PERIPH, &init);
    USART_Enable(UART_PERIPH, ENABLE);

    return RT_EOK;
}

/**
 * @brief 处理串口接收中断开关和运行时配置命令。
 * @param serial 串口对象；cmd 为 RT_DEVICE_CTRL_*；CONFIG 时 arg 指向 serial_configure。
 * @return 当前实现返回 RT_EOK，未识别命令不执行额外操作。
 * @details 用法：由串口框架 open/close/control 路径调用。
 *          动作：SET_INT/CLR_INT 控制 RXDNE 中断；CONFIG 转给 n32_uart_configure()。
 */
static rt_err_t n32_uart_control(struct rt_serial_device *serial, int cmd, void *arg)
{
    (void)arg;

    RT_ASSERT(serial != RT_NULL);

    switch (cmd)
    {
        case RT_DEVICE_CTRL_CLR_INT:
            /* 关接收中断 */
            USART_ConfigInt(UART_PERIPH, USART_INT_RXDNE, DISABLE);
            break;

        case RT_DEVICE_CTRL_SET_INT:
            /* 开接收中断 */
            USART_ConfigInt(UART_PERIPH, USART_INT_RXDNE, ENABLE);
            break;

        case RT_DEVICE_CTRL_CONFIG:
            /* 运行时改波特率等: 重新 configure */
            n32_uart_configure(serial, (struct serial_configure *)arg);
            break;

        default:
            break;
    }

    return RT_EOK;
}

/**
 * @brief 用轮询方式发送一个串口字符。
 * @param serial 非空串口对象；ch 为待发送字符。
 * @return 1，表示提交一个输入字符；换行会额外发送回车。
 * @details 用法：由 RT 串口框架发送路径调用，应用通常使用 rt_kprintf() 或 rt_device_write()。
 *          动作：等待 TXDE 后写发送寄存器；本实现忙等且无超时，不适合中断中发送大量文本。
 */
static int n32_uart_putc(struct rt_serial_device *serial, char ch)
{
    RT_ASSERT(serial != RT_NULL);

    /* msh 换行要发 \r\n, 否则终端上光标不回到行首 */
    if (ch == '\n')
    {
        while (USART_GetFlagStatus(UART_PERIPH, USART_FLAG_TXDE) == RESET)
        {
        }
        USART_SendData(UART_PERIPH, (uint16_t)'\r');
    }

    while (USART_GetFlagStatus(UART_PERIPH, USART_FLAG_TXDE) == RESET)
    {
    }
    USART_SendData(UART_PERIPH, (uint16_t)ch);

    return 1;
}

/**
 * @brief 非阻塞地从 USART1 接收寄存器取一个字符。
 * @param serial 非空串口设备对象。
 * @return 有数据时返回接收值，没有数据时返回 -1。
 * @details 用法：由 RT 串口中断处理框架调用以填充接收环形缓冲。
 *          动作：检查 RXDNE，只在有数据时读取接收寄存器，不等待下一字节。
 */
static int n32_uart_getc(struct rt_serial_device *serial)
{
    RT_ASSERT(serial != RT_NULL);

    if (USART_GetFlagStatus(UART_PERIPH, USART_FLAG_RXDNE) != RESET)
    {
        return (int)USART_ReceiveData(UART_PERIPH);
    }
    return -1;
}

static const struct rt_uart_ops s_n32_uart_ops =
{
    n32_uart_configure,
    n32_uart_control,
    n32_uart_putc,
    n32_uart_getc,
    RT_NULL, /* DMA transmit is not implemented; this port uses interrupt RX. */
};

/* ==========================================================================
 *  中断服务
 * ========================================================================== */
/**
 * @brief 将 USART1 接收中断交给 RT-Thread 串口框架。
 * @param serial 已注册的串口设备。
 * @details 用法：仅由 USART1_IRQHandler() 调用，处于硬件中断上下文。
 *          动作：确认 RXDNE 中断和数据标志均有效后发出 RT_SERIAL_EVENT_RX_IND，由框架读字节并通知上层。
 */
static void uart_isr(struct rt_serial_device *serial)
{
    RT_ASSERT(serial != RT_NULL);

    if ((USART_GetIntStatus(UART_PERIPH, USART_INT_RXDNE) != RESET) &&
        (USART_GetFlagStatus(UART_PERIPH, USART_FLAG_RXDNE) != RESET))
    {
        /* 交给框架: 它会读走字节塞进环形缓冲, 并回调 rx_indicate */
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_RX_IND);
    }
}

/**
 * @brief USART1 硬件中断入口，为 shell 接收提供驱动。
 * @details 用法：向量表自动调用，业务层无需自行轮询串口输入。
 *          动作：通知内核进入中断、执行 uart_isr()，再退出中断并允许唤醒接收线程。
 */
void USART1_IRQHandler(void)
{
    rt_interrupt_enter();
    uart_isr(s_uart);
    rt_interrupt_leave();
}

/* ==========================================================================
 *  注册
 * ========================================================================== */
/**
 * @brief 注册名为 usart1 的 RT-Thread 串口设备。
 * @return RT_EOK=成功，否则为注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动调用一次；控制台切换要在此注册之后。
 *          动作：设置默认 115200 波特率和 128 字节接收缓冲，绑定操作表，以读写和中断接收能力注册设备。
 * @note DMA 未实现，发送使用轮询；真正分配接收缓冲的步骤由带 INT_RX 标志的 open 完成。
 */
int rt_hw_usart_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    config.baud_rate = BAUD_RATE_115200;
    config.bufsz     = 128;

    s_uart->ops    = &s_n32_uart_ops;
    s_uart->config = config;

    return rt_hw_serial_register(s_uart,
                                 UART_DEVICE_NAME,
                                 RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                                 (void *)s_uart);
}
INIT_BOARD_EXPORT(rt_hw_usart_init);

#endif /* RT_USING_SERIAL */
