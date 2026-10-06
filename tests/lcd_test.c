/* Compile the actual GPIO driver against a trace hook at the bus boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "n32g45x.h"
#include "board_config.h"
#include "drv_lcd.h"
GPIO_Module mock_ports[5];
uint32_t SystemCoreClock=128000000;
static uint16_t trace_data[512];
static unsigned trace_rs[512], used, cs_low, rs_data;
void rt_thread_mdelay(int32_t ms) {(void)ms;}
void lcd_test_signal(unsigned signal, unsigned high)
{
    if (signal==0) cs_low=!high;
    if (signal==1) rs_data=high;
}
void lcd_test_write(uint16_t data)
{
    assert(cs_low);
    assert(used < 512);
    trace_data[used]=data; trace_rs[used++]=rs_data;
}
int main(void)
{
    assert(Gc9307cInit()==LCD_OK);
    assert(!cs_low);
    used=0;
    const uint16_t px[]={0xf800,0x07e0};
    Gc9307cFlush(0,319,1,319,px);
    const uint16_t expected[]={0x2a,0,0,0,1,0x2b,1,0x3f,1,0x3f,0x2c,0xf800,0x07e0};
    assert(used==sizeof(expected)/sizeof(expected[0]));
    assert(memcmp(trace_data,expected,sizeof(expected))==0);
    for (unsigned i=0;i<used;i++) assert(trace_rs[i]==(i==0||i==5||i==10?0:1));
    assert(!cs_low);
    used=0;
    Gc9307cFlush(240,0,240,0,px);
    Gc9307cFlush(1,0,0,0,px);
    Gc9307cFlush(0,0,0,0,NULL);
    assert(!used);
    Gc9307cFill(239,319,239,319,0x1234);
    assert(used==12 && trace_data[11]==0x1234 && !cs_low);
    assert(Gc9307cPixelNs()==93);
    puts("PASS: actual LCD driver CS, byte addressing, RGB565 pixels, bounds");
    return 0;
}
