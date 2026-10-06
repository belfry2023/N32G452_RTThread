#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../board/drv_gp21.c"

static struct rt_spi_device mock_spi;
static uint32_t chip_regs[7], chip_raw;
static uint16_t chip_status;
static int line=PIN_HIGH, irq_enabled, corrupt_id, fail_spi;
static unsigned writes, reads, init_commands;
rt_err_t rt_mutex_init(struct rt_mutex *m,const char *s,uint8_t f) {(void)s;(void)f;m->held=0;return 0;}
rt_err_t rt_mutex_take(struct rt_mutex *m,int t) {(void)t;assert(!m->held);m->held=1;return 0;}
rt_err_t rt_mutex_release(struct rt_mutex *m) {assert(m->held);m->held=0;return 0;}
rt_err_t rt_sem_init(struct rt_semaphore *s,const char *n,unsigned v,uint8_t f) {(void)n;(void)f;s->count=v;return 0;}
rt_err_t rt_sem_take(struct rt_semaphore *s,int t) {(void)t;if(!s->count)return -RT_ETIMEOUT;s->count--;return 0;}
rt_err_t rt_sem_release(struct rt_semaphore *s) {s->count++;return 0;}
rt_tick_t rt_tick_get(void) {return 123;}
rt_tick_t rt_tick_from_millisecond(int32_t ms) {return ms;}
void rt_thread_mdelay(int32_t ms) {(void)ms;}
rt_err_t rt_thread_init(struct rt_thread*t,const char*n,void(*e)(void*),void*a,void*s,unsigned z,uint8_t p,unsigned q)
{(void)t;(void)n;(void)e;(void)a;(void)s;(void)z;(void)p;(void)q;return 0;}
rt_err_t rt_thread_startup(struct rt_thread*t) {(void)t;return 0;}
rt_base_t rt_hw_interrupt_disable(void) {return 0;}
void rt_hw_interrupt_enable(rt_base_t l) {(void)l;}
rt_device_t rt_device_find(const char *s) {assert(!strcmp(s,"spi30"));return &mock_spi.parent;}
rt_err_t rt_device_register(rt_device_t d,const char*n,uint16_t f)
{assert(!strcmp(n,"tdc0"));assert(f & RT_DEVICE_FLAG_STANDALONE);d->flag=f;return 0;}
rt_err_t rt_spi_configure(struct rt_spi_device*d,struct rt_spi_configuration*c)
{(void)d;assert(c->mode==(RT_SPI_MODE_1|RT_SPI_MSB));assert(c->data_width==8);assert(c->max_hz==2000000);return 0;}
rt_size_t rt_spi_send(struct rt_spi_device*d,const void *data,rt_size_t size)
{
    (void)d; const uint8_t *b=data;
    if(fail_spi)return 0;
    line=PIN_HIGH; writes++;
    if(size==1) {
        if(b[0]==0x50) memset(chip_regs,0,sizeof(chip_regs));
        else {assert(b[0]==0x70);init_commands++;chip_status=0;}
    } else {
        assert(size==5 && (b[0]&0xf8)==0x80 && (b[0]&7)<7);
        chip_regs[b[0]&7]=((uint32_t)b[1]<<24)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<8)|b[4];
    }
    return size;
}
rt_err_t rt_spi_send_then_recv(struct rt_spi_device*d,const void*data,rt_size_t tx,void*out,rt_size_t size)
{
    (void)d; const uint8_t *b=data; uint8_t *p=out;
    if(fail_spi)return -RT_EIO;
    assert(tx==1);reads++;line=PIN_HIGH;
    switch(*b) {
    case 0xb7: assert(size==7);for(unsigned i=0;i<7;i++)p[i]=(uint8_t)chip_regs[i];if(corrupt_id)p[1]^=1;break;
    case 0xb5: assert(size==1);p[0]=chip_regs[1]>>24;break;
    case 0xb4: assert(size==2);p[0]=chip_status>>8;p[1]=chip_status;break;
    case 0xb0: assert(size==4);for(unsigned i=0;i<4;i++)p[i]=chip_raw>>(24-8*i);break;
    default: assert(0);
    }
    return 0;
}
void rt_pin_write(rt_base_t pin,int value) {(void)pin;(void)value;}
void rt_pin_mode(rt_base_t pin,int mode) {(void)pin;(void)mode;}
int rt_pin_read(rt_base_t pin) {assert(pin==BSP_TDC_INT_PIN);return line;}
rt_err_t rt_pin_irq_enable(rt_base_t pin,int en) {assert(pin==BSP_TDC_INT_PIN);irq_enabled=en;return 0;}
rt_err_t rt_pin_attach_irq(rt_base_t pin,int mode,void(*cb)(void*),void*a)
{(void)a;assert(pin==BSP_TDC_INT_PIN && mode==PIN_IRQ_MODE_FALLING && cb==irq_handler);return 0;}
static void complete(uint16_t status,uint32_t raw)
{
    /* Reject a reversed ALU selection even if SPI readback itself succeeds. */
    assert(((chip_regs[1] >> 24) & 15u) == 1u);
    assert(((chip_regs[1] >> 28) & 15u) == 2u);
    chip_status=status;chip_raw=raw;line=PIN_LOW;
    irq_handler(NULL);
    assert(acquire_once(RT_EOK,tdc.generation));
}
int main(void)
{
    assert(gp21_register()==RT_EOK);
    assert(tdc.config.reference_hz==5000000);
    corrupt_id=1;
    assert(device_open(&tdc.dev,RT_DEVICE_FLAG_RDONLY)==-RT_EIO && !tdc.opened);
    corrupt_id=0;
    assert(device_open(&tdc.dev,RT_DEVICE_FLAG_RDONLY)==0 && tdc.opened);
    assert(writes==16); /* reset + seven writes, repeated after failed probe */
    assert(device_control(&tdc.dev,GP21_CTRL_SELFTEST,NULL)==0);
    gp21_config_t config;
    assert(device_control(&tdc.dev,GP21_CTRL_GET_CONFIG,&config)==0);
    config.reference_hz=0;
    assert(device_control(&tdc.dev,GP21_CTRL_SET_CONFIG,&config)==-RT_EINVAL);
    assert(device_control(&tdc.dev,GP21_CTRL_GET_CONFIG,&config)==0);
    config.regs[1] = (config.regs[1] & 0xffffffu) | 0x12000000u;
    assert(device_control(&tdc.dev,GP21_CTRL_SET_CONFIG,&config)==-RT_EINVAL);
    assert(device_control(&tdc.dev,GP21_CTRL_GET_CONFIG,&config)==0);
    config.reference_hz=4000000;
    assert(device_control(&tdc.dev,GP21_CTRL_SET_CONFIG,&config)==0);
    assert(tdc.config.reference_hz==4000000);
    config.reference_hz=5000000;
    assert(device_control(&tdc.dev,GP21_CTRL_SET_CONFIG,&config)==0);
    assert(device_control(&tdc.dev,GP21_CTRL_START,NULL)==0 && irq_enabled);
    assert(device_control(&tdc.dev,GP21_CTRL_SELFTEST,NULL)==-RT_EBUSY);
    assert(device_control(&tdc.dev,GP21_CTRL_SET_CONFIG,&config)==-RT_EBUSY);
    gp21_sample_t samples[2];
    assert(device_read(&tdc.dev,0,samples,sizeof(samples))==0);
    unsigned prior_reads=reads;
    assert(!acquire_once(-RT_ETIMEOUT,tdc.generation));
    assert(tdc.stats.timeouts==1 && reads==prior_reads);
    complete(0x11,0x000a0000);
    assert(device_read(&tdc.dev,0,samples,sizeof(samples))==sizeof(samples[0]));
    assert(samples[0].time_ps==2000000 && samples[0].valid && samples[0].sequence==1);
    assert(device_read(&tdc.dev,0,samples,sizeof(samples))==0);
    complete(0x211,0xffffffff);
    assert(device_read(&tdc.dev,0,samples,sizeof(samples))==sizeof(samples[0]));
    assert(!samples[0].valid && samples[0].time_ps==0);
    for(unsigned i=0;i<18;i++)complete(0x11,0xa0000);
    assert(tdc.stats.dropped==2 && tdc.head-tdc.tail==16);
    assert(device_read(&tdc.dev,0,samples,sizeof(samples)-1)==sizeof(samples[0]));
    assert(device_read(&tdc.dev,1,samples,sizeof(samples))==0);
    assert(device_read(&tdc.dev,0,NULL,sizeof(samples))==0);
    uint32_t old_generation=tdc.generation;
    assert(device_control(&tdc.dev,GP21_CTRL_STOP,NULL)==0 && !irq_enabled);
    assert(device_control(&tdc.dev,GP21_CTRL_START,NULL)==0);
    unsigned prior_samples=tdc.stats.samples;
    assert(!acquire_once(RT_EOK,old_generation) && tdc.stats.samples==prior_samples);
    fail_spi=1;line=PIN_LOW;
    assert(!acquire_once(RT_EOK,tdc.generation));
    assert(tdc.stats.io_errors==2 && !tdc.stats.running && !irq_enabled);
    fail_spi=0;
    assert(device_close(&tdc.dev)==0 && !tdc.opened && !irq_enabled);
    assert(device_control(&tdc.dev,GP21_CTRL_START,NULL)==-RT_EIO);
    assert(device_open(&tdc.dev,RT_DEVICE_FLAG_RDONLY)==0 && tdc.head==tdc.tail);
    puts("PASS: actual GP21 driver probe/retry, IRQ acquisition, FIFO, timeout, generation cancellation, stop/reopen");
    return 0;
}
