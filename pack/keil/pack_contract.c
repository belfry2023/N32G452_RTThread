/* Reject common MDK configuration mismatches early, before producing an image. */
#include <rtthread.h>
#if !defined(__CC_ARM) && !defined(__ARMCC_VERSION)
#error Keil pack requires ARM Compiler 5 or ARM Compiler 6
#endif
#if !defined(__MICROLIB)
#error Enable Use MicroLIB in Options for Target
#endif
#if (defined(__CC_ARM) && defined(__TARGET_FPU_VFP)) || \
    (defined(__ARMCC_VERSION) && !defined(__CC_ARM) && !defined(__SOFTFP__))
#error Select software floating point for this pack
#endif
#if SYSCLK_SRC != 2 || SYSCLK_FREQ != 128000000
#error This N32G452VE board port requires HSI PLL at 128 MHz
#endif
#if RT_DEBUG_INIT != 1
#error This pack uses 8-byte RT init descriptors; keep RT_DEBUG_INIT at 1
#endif
