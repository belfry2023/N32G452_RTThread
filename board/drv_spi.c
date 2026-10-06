/**
 * @file    drv_spi.c
 * @brief   N32G452 SPI3 总线驱动 + 从设备挂载 (RT-Thread 版)
 *
 *  RT-Thread 的 SPI 框架分两层:
 *      总线 (rt_spi_bus)   —— 对应一个 SPI 控制器, 由本文件注册
 *      设备 (rt_spi_device) —— 挂在总线上的从设备, 由 rt_hw_spi_device_attach() 挂载
 *
 *  本文件实现两个回调 (rt_spi_ops):
 *      configure —— 把 RT-Thread 的 mode/max_hz 翻译成 N32 的 SPI 寄存器配置
 *      xfer      —— 按 rt_spi_message 描述收发数据, 并按 cs_take/cs_release 控制片选
 *
 *  ⚠️ 改引脚请见 inc/board_config.h
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "n32g45x.h"
#include "board_config.h"
#include "drv_spi.h"

/* ==========================================================================
 *  SPI3 引脚配置   ← 改引脚只改这一段
 *
 *  N32G452 数据手册引脚表:
 *      SPI3_SCK   默认 PC3    重映射 PC10 / PD9
 *      SPI3_MISO  默认 PA0    重映射 PC11
 *      SPI3_MOSI  默认 PA1    重映射 PC12
 *
 *  当前默认分配: MISO=PA0, MOSI=PA1 (默认组), SCK=PC3。
 *  若改用重映射组, 三件事必须同时做:
 *      1. 下面三个 *_PORT / *_PIN 改成新引脚
 *      2. 把 SPI3_REMAP_MACRO 改成对应的 GPIO_RMPx_SPI3
 *      3. 把 SPI3_REMAP_ENABLE 置 1
 * ========================================================================== */
#define SPI3_SCK_PORT BSP_SPI3_SCK_PORT
#define SPI3_SCK_PIN BSP_SPI3_SCK_PIN

#define SPI3_MISO_PORT BSP_SPI3_MISO_PORT
#define SPI3_MISO_PIN BSP_SPI3_MISO_PIN

#define SPI3_MOSI_PORT BSP_SPI3_MOSI_PORT
#define SPI3_MOSI_PIN BSP_SPI3_MOSI_PIN

/* 重映射: 用默认引脚组时置 0 */
#define SPI3_REMAP_ENABLE BSP_SPI3_REMAP_ENABLE
#define SPI3_REMAP_MACRO BSP_SPI3_REMAP

/* SPI3 挂在 APB1 上；HSI 128MHz 配置下 PCLK1=32MHz，SCK=PCLK1/分频值。 */
#define SPI3_ON_APB1        1

/* ==========================================================================
 *  CS 引脚信息: 通过 rt_spi_device 的 user_data 传给 xfer 回调
 * ========================================================================== */
struct n32_spi_cs
{
    GPIO_Module *port;
    uint16_t     pin;
};

/* ==========================================================================
 *  回调 1: 配置
 * ========================================================================== */
/**
 * @brief 把 RT-Thread SPI 配置转换为 N32 SPI3 主机寄存器设置。
 * @param device SPI 从设备；cfg 为模式、位宽和最大时钟配置，当前只接受 8 位主机模式。
 * @return RT_EOK=成功，非法参数或所需频率低于可用分频范围时返回负错误码。
 * @details 用法：应用调用 rt_spi_configure()，由框架在需要时应用配置，不直接调用本回调。
 *          动作：读取 APB1 时钟，选择满足 max_hz 的 2..256 分频，设置 CPOL/CPHA、位序、软件 NSS 后使能 SPI。
 */
static rt_err_t n32_spi_configure(struct rt_spi_device *device,
                                  struct rt_spi_configuration *cfg)
{
    SPI_InitType init;
    rt_uint32_t  pclk;
    rt_uint32_t  div;
    rt_uint32_t  pres;

    if ((device == RT_NULL) || (cfg == RT_NULL) || cfg->data_width != 8 ||
        !cfg->max_hz || (cfg->mode & RT_SPI_SLAVE))
    {
        return -RT_ERROR;
    }

    /* SPI3 在 APB1。使用 RCC_GetClocksFreqValue 而不是写死,
     * 这样改主频或改挂载总线时不用改这里。 */
    {
        RCC_ClocksType clocks;
        RCC_GetClocksFreqValue(&clocks);
        pclk = clocks.Pclk1Freq;
    }

    /* 由 max_hz 反推最小的合法分频 (SPI 只支持 2/4/8/.../256) */
    pres = SPI_BR_PRESCALER_256;
    for (div = 2u; div <= 256u; div <<= 1)
    {
        if ((pclk / div) <= cfg->max_hz)
        {
            switch (div)
            {
            case 2u:   pres = SPI_BR_PRESCALER_2;   break;
            case 4u:   pres = SPI_BR_PRESCALER_4;   break;
            case 8u:   pres = SPI_BR_PRESCALER_8;   break;
            case 16u:  pres = SPI_BR_PRESCALER_16;  break;
            case 32u:  pres = SPI_BR_PRESCALER_32;  break;
            case 64u:  pres = SPI_BR_PRESCALER_64;  break;
            case 128u: pres = SPI_BR_PRESCALER_128; break;
            default:   pres = SPI_BR_PRESCALER_256; break;
            }
            break;
        }
    }

    if (div > 256u) return -RT_EINVAL;
    SPI_Enable(SPI3, DISABLE);
    SPI_InitStruct(&init);                                  /* 先取默认值 */

    init.DataDirection = SPI_DIR_DOUBLELINE_FULLDUPLEX;
    init.SpiMode       = SPI_MODE_MASTER;
    init.DataLen       = (cfg->data_width == 16u) ? SPI_DATA_SIZE_16BITS
                                                  : SPI_DATA_SIZE_8BITS;
    init.FirstBit      = (cfg->mode & RT_SPI_MSB) ? SPI_FB_MSB : SPI_FB_LSB;

    /* RT-Thread 的 mode 低 2 位就是 CPOL/CPHA */
    switch (cfg->mode & 0x03u)
    {
    case 0u:                                            /* Mode 0 */
        init.CLKPOL = SPI_CLKPOL_LOW;
        init.CLKPHA = SPI_CLKPHA_FIRST_EDGE;
        break;
    case 1u:                                            /* Mode 1 */
        init.CLKPOL = SPI_CLKPOL_LOW;
        init.CLKPHA = SPI_CLKPHA_SECOND_EDGE;
        break;
    case 2u:                                            /* Mode 2 */
        init.CLKPOL = SPI_CLKPOL_HIGH;
        init.CLKPHA = SPI_CLKPHA_FIRST_EDGE;
        break;
    default:                                            /* Mode 3 */
        init.CLKPOL = SPI_CLKPOL_HIGH;
        init.CLKPHA = SPI_CLKPHA_SECOND_EDGE;
        break;
    }

    init.NSS            = SPI_NSS_SOFT;      /* 片选由 xfer 回调软件控制 */
    init.BaudRatePres   = pres;

    SPI_Init(SPI3, &init);
    SPI_Enable(SPI3, ENABLE);

    return RT_EOK;
}

/* ==========================================================================
 *  回调 2: 数据传输
 * ========================================================================== */
/**
 * @brief 用有限轮询次数等待 SPI3 状态位达到目标值。
 * @param flag 要查询的状态位；value 为期望的 SET 或 RESET。
 * @return 1=达到状态，0=轮询预算耗尽。
 * @details 用法：收发内部使用，防止外设异常时永远卡住。
 *          动作：按 SystemCoreClock 建立循环预算反复读状态；预算是循环次数，不是精确毫秒超时。
 */
static int spi_wait(uint16_t flag, FlagStatus value)
{
    /* Finite even before SysTick is available; covers a stopped peripheral. */
    uint32_t budget = SystemCoreClock / 100u;
    while (SPI_I2S_GetStatus(SPI3, flag) != value)
        if (!--budget) return 0;
    return 1;
}
/**
 * @brief 执行一条 SPI 消息，处理收发数据、软件片选和失败恢复。
 * @param device 已挂载的从设备；message 指定发送/接收缓冲、长度和 cs_take/cs_release。
 * @return 成功时为 message->length 字节；任何失败返回 0，不返回部分长度。
 * @details 用法：通过 rt_spi_send/recv/send_then_recv 等框架接口调用，框架负责总线互斥。
 *          动作：按标志拉低 CS，逐字节轮询收发；发送缓冲为空时发送 0xFF，接收缓冲为空时丢弃读值。
 *          结束时等 BUSY 清零再按标志释放 CS；超时会释放 CS、重启 SPI 并记录 errno。
 * @note 传输占用 CPU；该工程的 SPI 设备访问在线程中完成，不能在 GP21 EXTI 回调中直接收发。
 */
static rt_uint32_t n32_spi_xfer(struct rt_spi_device *device,
                              struct rt_spi_message *message)
{
    if (!device || !message) return 0;
    struct n32_spi_cs *cs = device->parent.user_data;
    const rt_uint8_t *tx = message->send_buf;
    rt_uint8_t *rx = message->recv_buf;
    if (message->cs_take && cs) cs->port->PBC = cs->pin;
    for (rt_size_t i = 0; i < message->length; ++i) {
        if (!spi_wait(SPI_I2S_TE_FLAG, SET)) goto fail;
        SPI_I2S_TransmitData(SPI3, tx ? tx[i] : 0xffu);
        if (!spi_wait(SPI_I2S_RNE_FLAG, SET)) goto fail;
        rt_uint8_t byte = SPI_I2S_ReceiveData(SPI3);
        if (rx) rx[i] = byte;
    }
    if (!spi_wait(SPI_I2S_BUSY_FLAG, RESET)) goto fail;
    if (message->cs_release && cs) {
        cs->port->PBSC = cs->pin;
        __DSB();
        for (volatile unsigned i = 0; i < 8; ++i) __NOP();
    }
    return message->length;
fail:
    if (cs) cs->port->PBSC = cs->pin;
    SPI_Enable(SPI3, DISABLE);
    (void)SPI_I2S_ReceiveData(SPI3);
    (void)SPI3->STS;
    SPI_Enable(SPI3, ENABLE);
    rt_set_errno(-RT_ETIMEOUT);
    /* Vendor SPI core only recognizes zero as failure, never return partial. */
    return 0;
}

static const struct rt_spi_ops n32_spi_ops =
{
    n32_spi_configure,
    n32_spi_xfer
};

/* ==========================================================================
 *  总线注册
 * ========================================================================== */
static struct rt_spi_bus s_spi3_bus;

/**
 * @brief 根据板级宏初始化 SPI3 引脚、时钟和可选重映射。
 * @details 用法：由 rt_hw_spi3_init() 调用，改接线先调整 board_config.h。
 *          动作：开启 GPIO/SPI3 时钟，必要时先开重映射，再将 SCK/MOSI 设为复用推挽、MISO 设为输入。
 * @note SPI 引脚必须匹配芯片允许的复用组合，不能只改成任意 GPIO 编号。
 */
static void spi3_gpio_init(void)
{
    GPIO_InitType gpio;

    RCC_EnableAPB2PeriphClk(BSP_GPIO_CLOCKS, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_SPI3, ENABLE);

#if SPI3_REMAP_ENABLE
    /* 重映射必须在配置复用引脚之前生效 */
    GPIO_ConfigPinRemap(SPI3_REMAP_MACRO, ENABLE);
#endif

    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* SCK / MOSI: 复用推挽 */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.Pin       = SPI3_SCK_PIN;
    GPIO_InitPeripheral(SPI3_SCK_PORT, &gpio);
    gpio.Pin = SPI3_MOSI_PIN;
    GPIO_InitPeripheral(SPI3_MOSI_PORT, &gpio);

    /* MISO: 浮空输入 */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.Pin       = SPI3_MISO_PIN;
    GPIO_InitPeripheral(SPI3_MISO_PORT, &gpio);
}

/**
 * @brief 注册 SPI3 总线对象及其 configure/xfer 回调。
 * @return RT_EOK=成功，否则返回总线注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动执行一次；应用查找的是从设备 spi30，而不是把总线当传感器用。
 *          动作：初始化引脚，注册 spi3，并读取一次接收寄存器清除残留数据。
 */
int rt_hw_spi3_init(void)
{
    spi3_gpio_init();

    /* "spi3" 是总线名; 应用层用 rt_device_find("spi30") 找设备 */
    rt_err_t err = rt_spi_bus_register(&s_spi3_bus, "spi3", &n32_spi_ops);
    if (err != RT_EOK) return err;

    /* 清一次接收缓冲, 去掉上电残留 */
    (void)SPI_I2S_ReceiveData(SPI3);

    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_hw_spi3_init);

/* ==========================================================================
 *  从设备挂载
 *
 *  框架不提供这个函数, 由 BSP 实现 (厂商应用笔记 §2.3.2 也是这个签名)。
 *  每个从设备需要一块静态的 rt_spi_device 和一份 CS 信息,
 *  所以用一个小对象池管理, 避免动态分配。
 * ========================================================================== */
#define SPI_DEV_MAX     4

static struct rt_spi_device s_spi_dev_pool[SPI_DEV_MAX];
static struct n32_spi_cs    s_spi_cs_pool[SPI_DEV_MAX];
static rt_uint8_t           s_spi_dev_used;

/**
 * @brief 为 SPI 总线挂载一个带独立低有效片选的从设备。
 * @param bus_name 已注册总线名；device_name 新设备名；cs_gpiox 为片选端口；cs_gpio_pin 为单引脚位掩码。
 * @return RT_EOK=成功；参数无效、静态池已满或挂载失败返回错误码。
 * @details 用法：总线初始化后串行调用，例如 rt_hw_spi_device_attach("spi3", "spi30", GPIOC, GPIO_PIN_4)。
 *          动作：从最多四个对象的静态池取位置，配置 CS 高电平空闲，再把设备和片选信息交给框架。
 * @note 当前 spi30 已自动挂载，不要再次使用同名设备；此辅助函数没有并发挂载保护。
 */
rt_err_t rt_hw_spi_device_attach(const char        *bus_name,
                                 const char        *device_name,
                                 GPIO_Module       *cs_gpiox,
                                 rt_uint16_t        cs_gpio_pin)
{
    struct rt_spi_device *dev;
    struct n32_spi_cs    *cs;
    GPIO_InitType         gpio;

    if ((bus_name == RT_NULL) || (device_name == RT_NULL) ||
        (cs_gpiox == RT_NULL) || !cs_gpio_pin || (cs_gpio_pin & (cs_gpio_pin - 1u)) ||
        (s_spi_dev_used >= SPI_DEV_MAX))
    {
        return -RT_ERROR;
    }

    dev = &s_spi_dev_pool[s_spi_dev_used];
    cs  = &s_spi_cs_pool[s_spi_dev_used];


    RCC_EnableAPB2PeriphClk(BSP_GPIO_CLOCKS, ENABLE);
    /* CS 引脚配成推挽输出, 空闲为高 */
    gpio.Pin        = cs_gpio_pin;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(cs_gpiox, &gpio);
    cs_gpiox->PBSC = cs_gpio_pin;

    cs->port = cs_gpiox;
    cs->pin  = cs_gpio_pin;

    /* 把设备挂到总线上; user_data 携带 CS 信息供 xfer 回调使用 */
    rt_err_t err = rt_spi_bus_attach_device(dev, device_name, bus_name, cs);
    if (err == RT_EOK) s_spi_dev_used++;
    return err;
}

/* ==========================================================================
 *  板级从设备挂载   ← 改片选引脚只改这一段
 *
 *  放在 INIT_DEVICE_EXPORT (第 3 阶段), 此时 INIT_BOARD_EXPORT (第 1 阶段)
 *  的 rt_hw_spi3_init() 已经执行过, 总线一定存在。
 * ========================================================================== */
#define TDC_CS_PORT BSP_TDC_CS_PORT
#define TDC_CS_PIN BSP_TDC_CS_PIN

#define TDC_SPI_BUS     "spi3"
#define TDC_SPI_DEV BSP_TDC_SPI_NAME

/**
 * @brief 把板上的 GP21 SPI 从设备挂到 spi3 总线。
 * @return RT_EOK=成功，挂载失败返回 -RT_ERROR。
 * @details 用法：INIT_DEVICE_EXPORT 自动执行一次，早于 GP21 组件注册。
 *          动作：使用 BSP_TDC_SPI_NAME 和片选宏调用挂载接口，打印实际片选端口/位号。
 */
int rt_hw_spi_devices_init(void)
{
    if (rt_hw_spi_device_attach(TDC_SPI_BUS, TDC_SPI_DEV,
                                TDC_CS_PORT, TDC_CS_PIN) != RT_EOK)
    {
        rt_kprintf("[drv_spi] 挂载 %s 失败\n", TDC_SPI_DEV);
        return -RT_ERROR;
    }

    unsigned cs_bit = 0;
    while ((TDC_CS_PIN >> cs_bit) != 1u) ++cs_bit;
    rt_kprintf("[drv_spi] %s 已挂到 %s, CS=%s%d\n",
               TDC_SPI_DEV, TDC_SPI_BUS,
               (TDC_CS_PORT == GPIOA) ? "PA" :
               (TDC_CS_PORT == GPIOB) ? "PB" :
               (TDC_CS_PORT == GPIOC) ? "PC" :
               (TDC_CS_PORT == GPIOD) ? "PD" : "PE",
               (int)cs_bit);

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_spi_devices_init);
