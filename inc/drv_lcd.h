/** GC9307C 240x320 RGB565 GPIO 8080-I driver.
 * Configure wiring in board_config.h and panel parameters in gc9307c_panel.h.
 * The complete 16-bit data port must differ from every control pin's port.
 * Use lcd0 rt_device_control in RT-Thread for serialized drawing.
 */
#ifndef __GC9307C_H__
#define __GC9307C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 屏幕参数 */
#define LCD_WIDTH           240u
#define LCD_HEIGHT          320u

/* RGB565 颜色 */
#define LCD_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
#define LCD_BLACK           0x0000u
#define LCD_WHITE           0xFFFFu
#define LCD_RED             0xF800u
#define LCD_GREEN           0x07E0u
#define LCD_BLUE            0x001Fu

/* 返回值 */
#define LCD_OK              0
#define LCD_ERR_PARAM      (-1)

/* ==========================================================================
 * 接口
 * ========================================================================== */

/**
 * @brief 初始化 GPIO 8080 总线并唤醒 GC9307C 屏幕。
 * @return LCD_OK=已发送初始化序列，LCD_ERR_PARAM=接线宏或方向配置无效。
 * @details 用法：RT 工程由 lcd0 的设备初始化回调调用；正常应用使用 rt_device_open()，不要反复手动初始化。
 *          动作：检查配置、配置 GPIO、硬复位、软复位、写模组参数、退出休眠、开启显示，最后打开背光。
 * @note 本实现只有写总线；返回成功不代表检测到了屏幕或验证了实际显示效果。延时要求线程上下文。
 */
int Gc9307cInit(void);

/**
 * @brief 同步发送一个矩形区域的 RGB565 像素。
 * @param x0 左边界；y0 上边界；x1 右边界；y1 下边界，均为包含端点的像素坐标。
 * @param px 连续逐行排列的像素数组，至少 (x1-x0+1)*(y1-y0+1) 个 uint16_t。
 * @details 用法：初始化后串行调用；RT 应用优先用 lcd0 的 LCD_CTRL_BLIT，让设备层负责互斥。
 *          动作：检查坐标/指针，选中屏幕并设置窗口，逐像素写总线后释放 CS；返回时传输已结束。
 * @note 越界或空指针直接忽略，不进行裁剪；底层函数本身没有锁，不能与 LVGL 并发绘制。
 */
void Gc9307cFlush(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                  const uint16_t *px);

/**
 * @brief 用单一 RGB565 颜色填充矩形。
 * @param x0 左边界；y0 上边界；x1 右边界；y1 下边界，均包含端点；color 为 RGB565 颜色。
 * @details 用法：屏幕初始化后在统一绘制线程或总线锁内调用，适合色条/清屏测试。
 *          动作：验证矩形边界，设置窗口，再重复写入同一颜色；坐标无效时忽略，不裁剪。
 */
void Gc9307cFill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief 使用一个 RGB565 颜色覆盖 240×320 整屏。
 * @param color 可使用 LCD_BLACK、LCD_WHITE 或 LCD_RGB565(r,g,b)。
 * @details 用法：初始化后串行调用，避免与 LVGL 同时绘制。
 *          动作：调用 Gc9307cFill() 填满全部坐标，执行期间同步占用 CPU 写总线。
 */
void Gc9307cClear(uint16_t color);

/**
 * @brief 按当前 APB2 时钟估算单像素 GPIO 写入耗时。
 * @return 纳秒估算值，按每像素三次 APB 访问、每次两个 PCLK2 周期计算。
 * @details 用法：用于初步分析带宽；不需要发送屏幕命令。
 *          动作：读取 RCC 时钟并计算 6×10^9/PCLK2，不包含循环、抢占和渲染开销，不是实测帧率。
 */
uint32_t Gc9307cPixelNs(void);

#ifdef __cplusplus
}
#endif

#endif /* __GC9307C_H__ */
