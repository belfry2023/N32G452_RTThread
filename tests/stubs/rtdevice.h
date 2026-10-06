#ifndef MOCK_RTDEVICE_H
#define MOCK_RTDEVICE_H
#include "rtthread.h"
struct rt_device;
typedef struct rt_device *rt_device_t;
struct rt_device {
    int type; uint16_t open_flag,flag;
    rt_err_t (*init)(rt_device_t);
    rt_err_t (*open)(rt_device_t,uint16_t);
    rt_err_t (*close)(rt_device_t);
    rt_size_t (*read)(rt_device_t,rt_off_t,void*,rt_size_t);
    rt_err_t (*control)(rt_device_t,int,void*);
    rt_err_t (*rx_indicate)(rt_device_t,rt_size_t);
};
struct rt_spi_device {struct rt_device parent;};
struct rt_spi_configuration {uint8_t mode,data_width;uint16_t reserved;uint32_t max_hz;};
#define RT_SPI_MODE_1 1
#define RT_SPI_MSB 4
#define RT_Device_Class_Sensor 19
#define RT_DEVICE_FLAG_RDONLY 1
#define RT_DEVICE_FLAG_STANDALONE 8
#define RT_DEVICE_OFLAG_MASK 15
#define PIN_LOW 0
#define PIN_HIGH 1
#define PIN_MODE_OUTPUT 1
#define PIN_MODE_INPUT_PULLUP 2
#define PIN_IRQ_MODE_FALLING 1
#define PIN_IRQ_DISABLE 0
#define PIN_IRQ_ENABLE 1
rt_device_t rt_device_find(const char*);
rt_err_t rt_device_register(rt_device_t,const char*,uint16_t);
rt_err_t rt_spi_configure(struct rt_spi_device*,struct rt_spi_configuration*);
rt_size_t rt_spi_send(struct rt_spi_device*,const void*,rt_size_t);
rt_err_t rt_spi_send_then_recv(struct rt_spi_device*,const void*,rt_size_t,void*,rt_size_t);
void rt_pin_write(rt_base_t,int);
void rt_pin_mode(rt_base_t,int);
int rt_pin_read(rt_base_t);
rt_err_t rt_pin_irq_enable(rt_base_t,int);
rt_err_t rt_pin_attach_irq(rt_base_t,int,void(*)(void*),void*);
#endif
