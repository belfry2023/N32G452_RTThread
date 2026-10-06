/* Keil ARM C library output. GCC src/syscalls.c must not be added to MDK. */
#include <stdio.h>
#include "n32g45x.h"

#if !defined(__MICROLIB)
#error This pack requires Use MicroLIB; see KEIL_PACK.md
#endif
#if defined(__CC_ARM)
#pragma import(__use_no_semihosting)
#else
__asm(".global __use_no_semihosting\n");
#endif

#if defined(__CC_ARM)
struct __FILE { int handle; }; /* AC6 MicroLIB stdio.h defines this already. */
#endif
FILE __stdout;
FILE __stdin;

void _ttywrch(int ch)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXDE) == RESET) { }
    USART_SendData(USART1, (uint16_t)(uint8_t)ch);
}
int fputc(int ch, FILE *stream)
{
    (void)stream;
    if (ch == '\n') _ttywrch('\r');
    _ttywrch(ch);
    return ch;
}
int fgetc(FILE *stream) { (void)stream; return EOF; }
int ferror(FILE *stream) { (void)stream; return 0; }
void _sys_exit(int code) { (void)code; for (;;) { __NOP(); } }
char *_sys_command_string(char *cmd, int len)
{
    if (len > 0) cmd[0] = '\0';
    return cmd;
}
