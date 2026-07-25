/*
 * Protected-mode entry.  The Linux/x86 setup code enters here with flat
 * 32-bit segments, paging disabled, and ESI pointing at boot_params.
 */

    .section .text.boot, "ax", @progbits
    .code32
    .globl _start
    .type _start, @function

_start:
    cli
    cld

    /* Preserve the boot parameter pointer while clearing kernel BSS. */
    movl    %esi, %ebx
    xorl    %eax, %eax
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx
    rep stosb

    movl    $__bootstrap_stack_top, %esp
    andl    $-16, %esp
    pushl   %ebx
    call    i386_boot_main

1:
    cli
    hlt
    jmp     1b

    .size _start, . - _start

    .section .note.GNU-stack, "", @progbits
