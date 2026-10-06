/**
 * @file    drv_spi.h
 * @brief   N32G452 SPI 总线驱动接口
 */
#ifndef __DRV_SPI_H__
#define __DRV_SPI_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "n32g45x.h"

/**
 * @brief 注册 SPI3 总线对象及其 configure/xfer 回调。
 * @return RT_EOK=成功，否则返回总线注册错误。
 * @details 用法：INIT_BOARD_EXPORT 自动执行一次；应用查找的是从设备 spi30，而不是把总线当传感器用。
 *          动作：初始化引脚，注册 spi3，并读取一次接收寄存器清除残留数据。
 */
int rt_hw_spi3_init(void);

/**
 * @brief 为 SPI 总线挂载一个带独立低有效片选的从设备。
 * @param bus_name 已注册总线名；device_name 新设备名；cs_gpiox 为片选端口；cs_gpio_pin 为单引脚位掩码。
 * @return RT_EOK=成功；参数无效、静态池已满或挂载失败返回错误码。
 * @details 用法：总线初始化后串行调用，例如 rt_hw_spi_device_attach("spi3", "spi30", GPIOC, GPIO_PIN_4)。
 *          动作：从最多四个对象的静态池取位置，配置 CS 高电平空闲，再把设备和片选信息交给框架。
 * @note 当前 spi30 已自动挂载，不要再次使用同名设备；此辅助函数没有并发挂载保护。
 */
rt_err_t rt_hw_spi_device_attach(const char  *bus_name,
                                 const char  *device_name,
                                 GPIO_Module *cs_gpiox,
                                 rt_uint16_t  cs_gpio_pin);

#endif /* __DRV_SPI_H__ */
