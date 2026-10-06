#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gp21_protocol.h"
int main(void)
{
    uint32_t regs[7]; uint8_t frame[5];
    gp21_default_config(regs);
    /* Independently decoded bitfields, not a copy of the literal config. */
    assert(((regs[0] >> 11) & 1) == 1); /* range 2 */
    assert(((regs[0] >> 12) & 1) == 0); /* auto calibration */
    assert(((regs[0] >> 13) & 1) == 1);
    assert(((regs[0] >> 18) & 3) == 1); /* clock continuously enabled */
    assert(((regs[0] >> 20) & 3) == 0); /* 5 MHz, /1 */
    /* V1.6 table 3-6 and example 6-2: mode 2 computes HIT2-HIT1. */
    assert(((regs[1] >> 24) & 15) == 1); /* HIT1 = start */
    assert(((regs[1] >> 28) & 15) == 2); /* HIT2 = first stop */
    assert(((regs[1] >> 16) & 7) == 2); /* including start */
    assert(((regs[2] >> 29) & 7) == 5); /* ALU + timeout */
    assert((regs[6] & 0x80000000u) == 0); /* digital stop */
    assert(gp21_write_frame(3, 0x12345678, frame)==0);
    const uint8_t expected[]={0x83,0x12,0x34,0x56,0x78};
    assert(memcmp(frame,expected,5)==0);
    assert(gp21_decode_u32(frame+1)==0x12345678u);
    assert(gp21_write_frame(7,0,frame)<0);
    assert(gp21_write_frame(0,0,NULL)<0);
    assert(gp21_result_ps(0x00010000,5000000,1)==200000); /* 200ns */
    assert(gp21_result_ps(0x00018000,5000000,1)==300000); /* 300ns */
    assert(gp21_result_ps(0x000a0000,5000000,1)==2000000); /* 2us */
    assert(gp21_result_ps(0x00010000,5000000,2)==400000);
    assert(gp21_result_ps(0xffff0000,5000000,4)==52428000000ULL);
    assert(gp21_result_ps(1,5000000,1)==3);
    assert(gp21_result_ps(0,0,1)==0);
    assert(gp21_result_valid(0x11,0xa0000));
    assert(!gp21_result_valid(0x211,0xa0000));
    assert(!gp21_result_valid(0x411,0xa0000));
    assert(!gp21_result_valid(0x10,0xa0000));
    assert(!gp21_result_valid(0x09,0xa0000));
    assert(!gp21_result_valid(0x11,0xffffffff));
    assert(!gp21_result_valid(0x11,0x80000000)); /* outside the mode-2 coarse-counter range */
    assert(!gp21_result_valid(0x11,0));
    assert(!gp21_result_valid(0x19,0xa0000)); /* unexpected hit count */
    puts("PASS: GP21 configuration, wire encoding, status rejection, 5MHz conversion");
    return 0;
}
