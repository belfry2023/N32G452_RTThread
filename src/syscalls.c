/**
 * @file    syscalls.c
 * @brief   newlib 系统调用桩 —— 让 printf / malloc 在裸机上可用
 *
 * 背景:
 *   链接使用 --specs=nosys.specs, 该 spec 提供的桩函数会打印
 *   "_write is not implemented and will always fail" 一类告警, 且 malloc 不可用。
 *   本文件提供强定义覆盖它们 (目标文件优先于库), 从而:
 *     - printf 输出重定向到调试串口
 *     - malloc/free 在 .bss 之后的空闲 RAM 中分配, 且不会撞进栈区
 */

#include "n32g45x.h"
#include <stdint.h>

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ==========================================================================
 * 标准输出重定向到 USART
 * ========================================================================== */

/**
 * @brief  newlib 的 printf 最终会调用到这里
 * @note   需先完成 RT-Thread 板级 USART1 初始化
 */
int _write(int file, char *ptr, int len)
{
    (void)file;

    for (int i = 0; i < len; i++)
    {
        /* 等待发送数据寄存器空 */
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXDE) == RESET)
        {
        }
        USART_SendData(USART1, (uint16_t)(uint8_t)ptr[i]);
    }

    /* 等待最后一个字节真正移出移位寄存器, 否则复位/休眠前会丢字符 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXC) == RESET)
    {
    }

    return len;
}

/* ==========================================================================
 * 堆管理
 *   __newlib_heap_start/end: linker-defined interval after static RAM,
 *   bounded below the reserved MSP stack; separate from the RT-Thread heap.
 * ========================================================================== */

extern unsigned char __newlib_heap_start[], __newlib_heap_end[];
static uintptr_t s_heap_ptr;

void *_sbrk(ptrdiff_t incr)
{
    uint32_t level = __get_PRIMASK();
    __disable_irq();
    uintptr_t base = (uintptr_t)__newlib_heap_start;
    uintptr_t limit = (uintptr_t)__newlib_heap_end;
    if (!s_heap_ptr) s_heap_ptr = base;
    uintptr_t amount = incr >= 0 ? (uintptr_t)incr : (uintptr_t)(-(incr + 1)) + 1u;
    if ((incr >= 0 && amount > limit - s_heap_ptr) ||
        (incr < 0 && amount > s_heap_ptr - base)) {
        __set_PRIMASK(level);
        errno = ENOMEM;
        return (void *)-1;
    }
    uintptr_t prev = s_heap_ptr;
    if (incr >= 0) s_heap_ptr += amount; else s_heap_ptr -= amount;
    __set_PRIMASK(level);
    return (void *)prev;
}

/* ==========================================================================
 * 其余桩函数: 裸机没有文件系统 / 进程模型, 给出确定的返回值即可
 * ========================================================================== */

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR; /* 字符设备, 让 newlib 认为是终端 */
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

off_t _lseek(int file, off_t offset, int whence)
{
    (void)file;
    (void)offset;
    (void)whence;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0; /* 无输入源 */
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}
