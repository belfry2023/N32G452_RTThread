/* Memory boundaries exported by the GCC linker script or Keil scatter file. */
#ifndef N32_RT_BOARD_H
#define N32_RT_BOARD_H
#include <stdint.h>
#include "n32g45x.h"
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
extern unsigned char Image$$RT_HEAP$$ZI$$Base[], Image$$RT_HEAP$$ZI$$Limit[];
extern unsigned char __Vectors[];
#define __rt_heap_start Image$$RT_HEAP$$ZI$$Base
#define __rt_heap_end   Image$$RT_HEAP$$ZI$$Limit
#define __vector_start __Vectors
#else
extern unsigned char __rt_heap_start[], __rt_heap_end[];
extern unsigned char __vector_start[];
#endif
#define N32_SRAM_HEAP_BEGIN ((uintptr_t)__rt_heap_start)
#define N32_SRAM_HEAP_END ((uintptr_t)__rt_heap_end)
#define N32_SRAM_HEAP_SIZE (N32_SRAM_HEAP_END - N32_SRAM_HEAP_BEGIN)
void rt_hw_board_init(void);
#endif
