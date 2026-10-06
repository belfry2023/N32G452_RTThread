/* Small standalone image for testing the real generated linker script's
 * assertions. This is not a replacement startup or a flashable firmware. */
.syntax unified
.cpu cortex-m4
.thumb
.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.thumb_func
Reset_Handler:
    b .
.section .isr_vector,"a",%progbits
.word _estack
.rept 101
.word Reset_Handler
.endr
.section .rti_fn.0,"a",%progbits
.word Reset_Handler
.section FSymTab,"a",%progbits
.word Reset_Handler
