/**
 * @file    board.c
 * @brief   RT-Thread 板级初始化 (N32G452)
 *
 *  rt_hw_board_init() 在调度器启动之前被调用, 负责:
 *      1. 设置向量表
 *      2. 配置 SysTick 作为 RT-Thread 的心跳
 *      3. 初始化内核堆
 *      4. 调用板级初始化项 (INIT_BOARD_EXPORT)
 *
 *  ⚠️ 本文件只做"内核跑起来"必需的事。
 *     具体外设(GPIO 复用、SPI、LCD、TDC)的初始化放在别处:
 *       - SPI 总线      -> board/drv_spi.c        (INIT_BOARD_EXPORT)
 *       - LCD 控制器    -> board/drv_lcd_device.c
 *       - TDC 设备挂载  -> board/drv_gp21.c
 *     修改 IO 口请见 inc/board_config.h
 */

#include <rthw.h>
#include <rtthread.h>

#include "n32g45x.h"
#include "board_config.h"
#include "board.h"

/* ==========================================================================
 *  控制台串口配置  ← 改串口只改这几行
 * ========================================================================== */
#define CONSOLE_UART            USART1
#define CONSOLE_UART_CLK        RCC_APB2_PERIPH_USART1
#define CONSOLE_UART_GPIO_CLK BSP_GPIO_CLOCKS
#define CONSOLE_UART_GPIO BSP_CONSOLE_PORT
#define CONSOLE_UART_TX_PIN BSP_CONSOLE_TX
#define CONSOLE_UART_RX_PIN BSP_CONSOLE_RX
#define CONSOLE_UART_BAUD BSP_CONSOLE_BAUD

/* ==========================================================================
 *  控制台输出
 *
 *  内核里 rt_hw_console_output() 是 RT_WEAK 的(见 src/kservice.c),
 *  在这里给出强定义即可让 rt_kprintf 直接往串口写,
 *  不需要实现完整的 rt_serial_ops 设备驱动。
 * ========================================================================== */
/**
 * @brief 在串口设备尚未接管控制台时提供底层文本输出。
 * @param str 有效的零结尾字符串；空指针时不输出。
 * @details 用法：由 RT-Thread 控制台路径调用，应用通常使用 rt_kprintf()。
 *          动作：轮询 USART1 发送空位，遇到换行补回车，末尾等待发送完成。
 * @note 此路径会忙等，不适合中断中的长日志；串口注册后通常改走串口设备输出路径。
 */
void rt_hw_console_output(const char *str)
{
    if (str == RT_NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        if (*str == '\n')
        {
            while (USART_GetFlagStatus(CONSOLE_UART, USART_FLAG_TXDE) == RESET)
            {
            }
            USART_SendData(CONSOLE_UART, (uint16_t)'\r');
        }

        while (USART_GetFlagStatus(CONSOLE_UART, USART_FLAG_TXDE) == RESET)
        {
        }
        USART_SendData(CONSOLE_UART, (uint16_t)(*str));

        str++;
    }

    /* 等最后一个字节移出, 否则复位/休眠前会丢字符 */
    while (USART_GetFlagStatus(CONSOLE_UART, USART_FLAG_TXC) == RESET)
    {
    }
}

/**
 * @brief 配置早期调试控制台，便于内核初始化期间打印信息。
 * @details 用法：由 rt_hw_board_init() 在串口设备注册之前调用。
 *          动作：开启 GPIO/USART1 时钟，配置 TX/RX、宏指定波特率和 8N1 格式，再使能串口。
 * @note board_config.h 的波特率用于此早期路径；完整串口设备的默认配置另见 drv_usart.c。
 */
static void console_uart_init(void)
{
    GPIO_InitType  gpio;
    USART_InitType uart;

    RCC_EnableAPB2PeriphClk(CONSOLE_UART_GPIO_CLK, ENABLE);
    RCC_EnableAPB2PeriphClk(CONSOLE_UART_CLK, ENABLE);

    /* TX: 复用推挽 */
    gpio.Pin        = CONSOLE_UART_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(CONSOLE_UART_GPIO, &gpio);

    /* RX: 浮空输入 */
    gpio.Pin       = CONSOLE_UART_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitPeripheral(CONSOLE_UART_GPIO, &gpio);

    uart.BaudRate            = CONSOLE_UART_BAUD;
    uart.WordLength          = USART_WL_8B;
    uart.StopBits            = USART_STPB_1;
    uart.Parity              = USART_PE_NO;
    uart.Mode                = USART_MODE_TX | USART_MODE_RX;
    uart.HardwareFlowControl = USART_HFCTRL_NONE;
    USART_Init(CONSOLE_UART, &uart);
    USART_Enable(CONSOLE_UART, ENABLE);
}

/* ==========================================================================
 *  系统心跳
 *
 *  SysTick 仅由此处驱动 RT-Thread 内核时基。
 * ========================================================================== */
/**
 * @brief Cortex-M 系统节拍中断，为 RT-Thread 推进时间。
 * @details 用法：由硬件中断向量自动进入，应用不要主动调用或再定义同名函数。
 *          动作：记录进入中断、调用 rt_tick_increase() 更新 tick/定时事件、记录退出中断并允许调度。
 */
void SysTick_Handler(void)
{
    /* 通知内核进入中断 */
    rt_interrupt_enter();

    rt_tick_increase();

    /* 通知内核离开中断（可能触发线程切换） */
    rt_interrupt_leave();
}

/* ==========================================================================
 *  板级初始化
 * ========================================================================== */
/**
 * @brief 在调度器启动前建立 RT-Thread 所需的板级基础环境。
 * @details 用法：由 rtthread_startup() 调用一次，应用不手动调用。
 *          动作：设置向量表、早期串口、内核堆、SysTick，再执行 INIT_BOARD_EXPORT 注册的驱动初始化。
 *          最后带接收中断标志打开控制台串口设备，并将 rt_kprintf 的输出切到该设备。
 * @note 此阶段尚不能使用线程延时；堆边界取自 board.h/链接脚本，修改内存划分需同时检查链接布局。
 */
void rt_hw_board_init(void)
{
    /* --- 1. 向量表 --- */
    /* SystemInit() 已由启动文件在进入 main 前调用, 时钟已配好 */
    SCB->VTOR = (uint32_t)__vector_start;
    __DSB();
    __ISB();

    /* --- 2. 控制台 (尽早, 便于后续打印调试信息) --- */
    console_uart_init();

    /* --- 3. 内核堆 --- */
    /* 注意: 堆在 SRAM 上半, 低半留给 .data/.bss/主栈, 见 board.h 与链接脚本 */
    rt_system_heap_init((void *)N32_SRAM_HEAP_BEGIN, (void *)N32_SRAM_HEAP_END);

    /* --- 4. SysTick: 每个 tick 一次中断, 驱动内核调度 --- */
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

    /* --- 5. 板级初始化项 (INIT_BOARD_EXPORT 注册的, 如 drv_spi / drv_gpio / drv_usart) --- */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

    /* --- 6. 把控制台切到串口设备 ---
     * 顺序很关键: 必须在 rt_components_board_init() 之后,
     * 因为 "usart1" 设备是 board/drv_usart.c 用 INIT_BOARD_EXPORT 注册的。
     *
     * ⚠️ rt_console_set_device() 内部只用 RT_DEVICE_FLAG_STREAM 打开设备,
     *    不会分配接收环形缓冲 —— 那样 msh 只能打印、收不到键盘输入。
     *    所以这里先自己带 INT_RX 打开一次。 */
#ifdef RT_USING_SERIAL
    {
        rt_device_t console = rt_device_find(RT_CONSOLE_DEVICE_NAME);

        if (console != RT_NULL)
        {
            rt_device_open(console, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
        }
    }
#endif

#ifdef RT_USING_CONSOLE
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif
}
