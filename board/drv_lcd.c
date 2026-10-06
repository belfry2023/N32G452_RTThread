/** GC9307C 240x320 RGB565, GPIO 8080-I 16-bit bus.
 * Wiring is in inc/board_config.h. Bus writes latch on WR rising edges.
 * Low-level functions must be serialized (lcd0 device provides the mutex).
 */
#include "n32g45x.h"
#include "board_config.h"
#include "drv_lcd.h"
#include "gc9307c_panel.h"
#ifdef N32_USE_RTTHREAD
#include <rtthread.h>
#endif

/* ==========================================================================
 *  ⚠️⚠️⚠️  接线配置 —— 按你的实际硬件核对  ⚠️⚠️⚠️
 * ========================================================================== */

/* The data bus occupies a complete port, separate from every control pin. */
#define LCD_DATA_PORT BSP_LCD_DATA_PORT

/* --- WR：PD5，普通 GPIO ---
 * 数据手册引脚表：PD5 复用功能仅 USART2_TX，无 TIMx 通道，故只能软件翻转 */
#define LCD_WR_PORT BSP_LCD_WR_PORT
#define LCD_WR_PIN BSP_LCD_WR_PIN

#define LCD_RS_PORT BSP_LCD_RS_PORT
#define LCD_RS_PIN BSP_LCD_RS_PIN
#define LCD_CS_PORT BSP_LCD_CS_PORT
#define LCD_CS_PIN BSP_LCD_CS_PIN
#define LCD_RST_PORT BSP_LCD_RESET_PORT
#define LCD_RST_PIN BSP_LCD_RESET_PIN
#define LCD_BL_PORT BSP_LCD_BL_PORT
#define LCD_BL_PIN BSP_LCD_BL_PIN
#define LCD_RD_PORT BSP_LCD_RD_PORT
#define LCD_RD_PIN BSP_LCD_RD_PIN

/* ==========================================================================
 *  时序参数 —— 来自 GC9307C 数据手册 (docs/gc9307c.pdf)
 * --------------------------------------------------------------------------
 *  8080-I/8080-II parallel interface timing table:
 *      twc   写周期                  min 66 ns
 *      twrl  WR 低电平宽度            min 15 ns
 *      twrh  WR 高电平宽度            min 15 ns
 *      tdst  数据建立时间              min 10 ns
 *      tdht  数据保持时间              min 10 ns
 *
 *  Default HSI/PLL: PCLK2=64 MHz. Three APB writes at an assumed two
 *  clocks/write give a 93 ns/pixel lower-bound estimate, not measured FPS.
 *  Verify WR/data setup/hold with a scope on the actual PCB.
 */
#define LCD_WR_LOW_PAD() do { __NOP(); } while (0)

/* ==========================================================================
 * 配置自检
 *
 * 注意: 不能用 #if 检查端口 —— LCD_DATA_PORT / GPIO_PIN_x 都是带强制类型转换的
 *       地址常量, 预处理器无法求值。改为在 Gc9307cInit() 里做运行时检查。
 * ========================================================================== */
/**
 * @brief 在访问 LCD 总线前检查引脚冲突和屏幕方向配置。
 * @return 1=检查通过，0=引脚重叠、位掩码无效或启用了不支持的横纵轴交换。
 * @details 用法：由 Gc9307cInit() 首先调用，修改 board_config.h 后会重新检查。
 *          动作：确认控制脚不占数据整口、各控制脚互不重复且都是单引脚，当前只支持 240×320 竖屏。
 */
static int lcd_config_check(void)
{
    GPIO_Module *const ports[] = {LCD_WR_PORT, LCD_RS_PORT, LCD_CS_PORT,
        LCD_RST_PORT, LCD_BL_PORT, LCD_RD_PORT};
    const uint16_t pins[] = {LCD_WR_PIN, LCD_RS_PIN, LCD_CS_PIN,
        LCD_RST_PIN, LCD_BL_PIN, LCD_RD_PIN};
    /* MV swaps the dimensions; this port implements portrait 240x320 only. */
    if (BSP_LCD_MADCTL & 0x20u) return 0;
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (ports[i] == LCD_DATA_PORT || !pins[i] || (pins[i] & (pins[i] - 1u))) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (ports[i] == ports[j] && pins[i] == pins[j]) return 0;
    }
    return 1;
}

#define LCD_CS_LOW()    (LCD_CS_PORT->PBC  = LCD_CS_PIN)
#define LCD_CS_HIGH()   (LCD_CS_PORT->PBSC = LCD_CS_PIN)
#define LCD_RS_CMD()    (LCD_RS_PORT->PBC  = LCD_RS_PIN)
#define LCD_RS_DATA()   (LCD_RS_PORT->PBSC = LCD_RS_PIN)

/* Host verification observes the real command stream, with no hardware writes. */
#ifdef N32_LCD_TRACE_TEST
void lcd_test_signal(unsigned signal, unsigned high);
void lcd_test_write(uint16_t data);
#undef LCD_CS_LOW
#undef LCD_CS_HIGH
#undef LCD_RS_CMD
#undef LCD_RS_DATA
#define LCD_CS_LOW() lcd_test_signal(0, 0)
#define LCD_CS_HIGH() lcd_test_signal(0, 1)
#define LCD_RS_CMD() lcd_test_signal(1, 0)
#define LCD_RS_DATA() lcd_test_signal(1, 1)
#endif

/* ==========================================================================
 * 工具
 * ========================================================================== */
/**
 * @brief 等待屏幕复位或退出休眠所需的毫秒时间。
 * @param ms 延时毫秒数。
 * @details 用法：内部初始化流程调用；RT-Thread 模式必须在线程中，不能在中断中调用。
 *          动作：RT 模式使用 rt_thread_mdelay() 让出 CPU；裸机分支使用 DWT 周期计数忙等。
 */
static void lcd_delay_ms(uint32_t ms)
{
#ifdef N32_USE_RTTHREAD
    rt_thread_mdelay(ms);
#else
    /* DWT supplies cycle-counted reset/sleep timings on Cortex-M4. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    while (ms--) {
        uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < SystemCoreClock / 1000u) {}
    }
#endif
}

/**
 * @brief 按当前 APB2 时钟估算单像素 GPIO 写入耗时。
 * @return 纳秒估算值，按每像素三次 APB 访问、每次两个 PCLK2 周期计算。
 * @details 用法：用于初步分析带宽；不需要发送屏幕命令。
 *          动作：读取 RCC 时钟并计算 6×10^9/PCLK2，不包含循环、抢占和渲染开销，不是实测帧率。
 */
uint32_t Gc9307cPixelNs(void)
{
    RCC_ClocksType clocks;
    RCC_GetClocksFreqValue(&clocks);
    return (uint32_t)(6000000000ULL / clocks.Pclk2Freq);
}

/* ==========================================================================
 * 8080 总线最底层 —— 整个驱动的性能热点
 * ========================================================================== */
/**
 * @brief 在 GPIO 模拟的 8080-I 总线上写一个 16 位数据字。
 * @param value 数据总线 D0..D15 的电平值；像素数据直接使用 RGB565。
 * @details 用法：内部热点函数；调用者先设置 CS/RS，且必须串行访问整条 LCD 总线。
 *          动作：WR 拉低、整口写入数据、插入保持间隔、WR 拉高；屏幕在 WR 上升沿锁存。
 */
static inline void lcd_bus_write(uint16_t value)
{
#ifdef N32_LCD_TRACE_TEST
    lcd_test_write(value);
#endif
    /* 顺序不能变:
     *   1) WR 拉低  —— 之后改数据不会被锁存
     *   2) 数据上线
     *   3) WR 拉高  —— 上升沿锁存, 数据建立时间 = 步骤2 到 3 的间隔 */
    LCD_WR_PORT->PBC   = LCD_WR_PIN;
    LCD_DATA_PORT->POD = value;
    LCD_WR_LOW_PAD();
    LCD_WR_PORT->PBSC  = LCD_WR_PIN;
}

/**
 * @brief 向屏幕发送一个 8 位命令码。
 * @param cmd GC9307C 命令码。
 * @details 用法：调用前需选中屏幕，内部使用，不单独处理片选或加锁。
 *          动作：把 RS 设为命令状态，再通过低 8 位数据线发送命令。
 */
static void lcd_write_cmd(uint8_t cmd)
{
    LCD_RS_CMD();
    lcd_bus_write((uint16_t)cmd);
}

/**
 * @brief 向屏幕发送一个 8 位命令参数。
 * @param d 参数字节。
 * @details 用法：紧跟相应命令调用，调用者负责 CS 和总线互斥。
 *          动作：把 RS 设为数据状态，通过一次总线写传输参数。
 */
static void lcd_write_data8(uint8_t d)
{
    LCD_RS_DATA();
    lcd_bus_write((uint16_t)d);
}

/**
 * @brief 将 16 位地址参数拆为两个 8 位参数发送。
 * @param d 列地址或行地址值，高字节先传。
 * @details 用法：由 lcd_set_window() 设置坐标时调用。
 *          动作：先发送 d 的高 8 位，再发送低 8 位；像素 RGB565 则直接走一次 lcd_bus_write()。
 */
static void lcd_write_data16(uint16_t d)
{
    /* Address parameters remain 8-bit even on a 16-bit pixel bus. */
    lcd_write_data8((uint8_t)(d >> 8));
    lcd_write_data8((uint8_t)d);
}

/* ==========================================================================
 * GPIO 初始化
 * ========================================================================== */
/**
 * @brief 初始化整口数据线和 LCD 控制 GPIO。
 * @details 用法：由 Gc9307cInit() 在配置检查通过后调用。
 *          动作：开 GPIO 时钟，将 16 位数据口和 WR/RS/CS/RST/BL/RD 设为推挽输出，建立空闲电平。
 * @note 复位期间关闭背光；数据口会整口配置和写入，不能再复用给其他设备。
 */
static void lcd_gpio_init(void)
{
    GPIO_InitType gpio;
    uint16_t      i;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO
                                | RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB
                                | RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_GPIOD
                                | RCC_APB2_PERIPH_GPIOE,
                            ENABLE);

    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    /* 数据总线 D0~D15 */
    for (i = 0u; i < 16u; i++)
    {
        gpio.Pin = (uint16_t)(1u << i);
        GPIO_InitPeripheral(LCD_DATA_PORT, &gpio);
    }
    LCD_DATA_PORT->POD = 0x0000u;

    /* WR (PD5) */
    gpio.Pin = LCD_WR_PIN;
    GPIO_InitPeripheral(LCD_WR_PORT, &gpio);
    LCD_WR_PORT->PBSC = LCD_WR_PIN;      /* 空闲态为高 */

    /* RS / CS / RES / BL */
    gpio.Pin = LCD_RS_PIN;
    GPIO_InitPeripheral(LCD_RS_PORT, &gpio);
    gpio.Pin = LCD_CS_PIN;
    GPIO_InitPeripheral(LCD_CS_PORT, &gpio);
    gpio.Pin = LCD_RST_PIN;
    GPIO_InitPeripheral(LCD_RST_PORT, &gpio);
    gpio.Pin = LCD_BL_PIN;
    GPIO_InitPeripheral(LCD_BL_PORT, &gpio);

    gpio.Pin = LCD_RD_PIN;
    GPIO_InitPeripheral(LCD_RD_PORT, &gpio);
    LCD_RD_PORT->PBSC = LCD_RD_PIN;
    LCD_CS_HIGH();
    LCD_RS_CMD();
    LCD_RST_PORT->PBSC = LCD_RST_PIN;
    LCD_BL_PORT->PBC = LCD_BL_PIN; /* Keep backlight off through panel reset. */
}

/**
 * @brief 执行 GC9307C 硬件复位时序。
 * @details 用法：控制脚初始化后在线程中调用。
 *          动作：RST 高电平 10 ms、低电平 20 ms、恢复高电平后等待 120 ms，再允许写初始化命令。
 */
static void lcd_reset(void)
{
    LCD_RST_PORT->PBSC = LCD_RST_PIN;
    lcd_delay_ms(10u);
    LCD_RST_PORT->PBC = LCD_RST_PIN;
    lcd_delay_ms(20u);
    LCD_RST_PORT->PBSC = LCD_RST_PIN;
    lcd_delay_ms(120u);
}

/* Panel defaults and gamma are separate from bus wiring. */
/**
 * @brief 写入屏幕模组初始化表、方向、像素格式和反色设置。
 * @details 用法：由 Gc9307cInit() 在选中屏幕后调用；模组电源/gamma 表在 gc9307c_panel.h 修改。
 *          动作：逐项发送 panel_init，再配置 MADCTL、RGB565 格式和显示反色命令。
 * @note 当前表是基线参数，具体模组的颜色、方向和 gamma 仍需实屏确认。
 */
static void lcd_vendor_init(void)
{
    for (unsigned i = 0; i < sizeof(panel_init) / sizeof(panel_init[0]); ++i) {
        lcd_write_cmd(panel_init[i].cmd);
        for (unsigned j = 0; j < panel_init[i].count; ++j)
            lcd_write_data8(panel_init[i].data[j]);
    }
    lcd_write_cmd(0x36u);
    lcd_write_data8(BSP_LCD_MADCTL);
    lcd_write_cmd(0x3au);
    lcd_write_data8(0x05u);
    lcd_write_cmd(BSP_LCD_INVERT ? 0x21u : 0x20u);
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */
/**
 * @brief 初始化 GPIO 8080 总线并唤醒 GC9307C 屏幕。
 * @return LCD_OK=已发送初始化序列，LCD_ERR_PARAM=接线宏或方向配置无效。
 * @details 用法：RT 工程由 lcd0 的设备初始化回调调用；正常应用使用 rt_device_open()，不要反复手动初始化。
 *          动作：检查配置、配置 GPIO、硬复位、软复位、写模组参数、退出休眠、开启显示，最后打开背光。
 * @note 本实现只有写总线；返回成功不代表检测到了屏幕或验证了实际显示效果。延时要求线程上下文。
 */
int Gc9307cInit(void)
{
    /* Reject overlapping pins before touching the bus. */
    if (lcd_config_check() == 0)
    {
        return LCD_ERR_PARAM;
    }

    lcd_gpio_init();
    lcd_reset();

    LCD_CS_LOW();

    lcd_write_cmd(0x01u);              /* SWRESET */
    lcd_delay_ms(120u);

    lcd_vendor_init();
    lcd_write_cmd(0x11u);              /* SLPOUT */
    lcd_delay_ms(120u);

    lcd_write_cmd(0x13u);              /* NORON — 正常显示模式 */
    lcd_delay_ms(10u);

    lcd_write_cmd(0x29u);              /* DISPON — 开显示 */
    lcd_delay_ms(50u);

    LCD_CS_HIGH();
    LCD_BL_PORT->PBSC = LCD_BL_PIN;    /* 开背光 */

    return LCD_OK;
}

/**
 * @brief 设置接下来写像素的矩形窗口。
 * @param x0 左边界；y0 上边界；x1 右边界；y1 下边界，四个坐标均包含端点。
 * @details 用法：内部调用，坐标先由上层验证，且 CS 已拉低。
 *          动作：发送 0x2A 列范围、0x2B 行范围，最后发送 0x2C 进入显存写入状态。
 */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2Au);              /* CASET 列地址 */
    lcd_write_data16(x0);
    lcd_write_data16(x1);

    lcd_write_cmd(0x2Bu);              /* RASET 行地址 */
    lcd_write_data16(y0);
    lcd_write_data16(y1);

    lcd_write_cmd(0x2Cu);              /* RAMWR 开始写显存 */
}

/**
 * @brief 同步发送一个矩形区域的 RGB565 像素。
 * @param x0 左边界；y0 上边界；x1 右边界；y1 下边界，均为包含端点的像素坐标。
 * @param px 连续逐行排列的像素数组，至少 (x1-x0+1)*(y1-y0+1) 个 uint16_t。
 * @details 用法：初始化后串行调用；RT 应用优先用 lcd0 的 LCD_CTRL_BLIT，让设备层负责互斥。
 *          动作：检查坐标/指针，选中屏幕并设置窗口，逐像素写总线后释放 CS；返回时传输已结束。
 * @note 越界或空指针直接忽略，不进行裁剪；底层函数本身没有锁，不能与 LVGL 并发绘制。
 */
void Gc9307cFlush(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                  const uint16_t *px)
{
    uint32_t n;

    if ((px == 0) || (x1 >= LCD_WIDTH) || (y1 >= LCD_HEIGHT) || (x0 > x1) || (y0 > y1))
    {
        return;
    }

    n = ((uint32_t)(x1 - x0) + 1u) * ((uint32_t)(y1 - y0) + 1u);

    LCD_CS_LOW();
    lcd_set_window(x0, y0, x1, y1);
    LCD_RS_DATA();

    while (n-- > 0u)
    {
        lcd_bus_write(*px++);
    }
    LCD_CS_HIGH();
}

/**
 * @brief 用单一 RGB565 颜色填充矩形。
 * @param x0 左边界；y0 上边界；x1 右边界；y1 下边界，均包含端点；color 为 RGB565 颜色。
 * @details 用法：屏幕初始化后在统一绘制线程或总线锁内调用，适合色条/清屏测试。
 *          动作：验证矩形边界，设置窗口，再重复写入同一颜色；坐标无效时忽略，不裁剪。
 */
void Gc9307cFill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t n;

    if ((x1 >= LCD_WIDTH) || (y1 >= LCD_HEIGHT) || (x0 > x1) || (y0 > y1))
    {
        return;
    }

    n = ((uint32_t)(x1 - x0) + 1u) * ((uint32_t)(y1 - y0) + 1u);

    LCD_CS_LOW();
    lcd_set_window(x0, y0, x1, y1);
    LCD_RS_DATA();

    while (n-- > 0u)
    {
        lcd_bus_write(color);
    }
    LCD_CS_HIGH();
}

/**
 * @brief 使用一个 RGB565 颜色覆盖 240×320 整屏。
 * @param color 可使用 LCD_BLACK、LCD_WHITE 或 LCD_RGB565(r,g,b)。
 * @details 用法：初始化后串行调用，避免与 LVGL 同时绘制。
 *          动作：调用 Gc9307cFill() 填满全部坐标，执行期间同步占用 CPU 写总线。
 */
void Gc9307cClear(uint16_t color)
{
    Gc9307cFill(0u, 0u, LCD_WIDTH - 1u, LCD_HEIGHT - 1u, color);
}
