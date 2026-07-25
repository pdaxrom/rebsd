#include "user_bootstrap.h"

    .section .text, "ax", @progbits
    .code32
    .globl _start
    .type _start, @function
_start:
    cmpl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_data
    jne     .Lfailed
    cmpl    $0, bootstrap_bss
    jne     .Lfailed
    movl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_bss
    cmpl    $I386_BOOTSTRAP_USER_DATA_MAGIC, bootstrap_bss
    jne     .Lfailed
    movl    $20, %eax
    int     $0x80
    movl    $I386_BOOTSTRAP_USER_RETURN_MAGIC, %ebx
    int     $0x30
    ud2
.Lfailed:
    xorl    %ebx, %ebx
    int     $0x30
    ud2
    .size _start, . - _start

    .section .data, "aw", @progbits
    .p2align 2
bootstrap_data:
    .long I386_BOOTSTRAP_USER_DATA_MAGIC

    .section .bss, "aw", @nobits
    .p2align 2
bootstrap_bss:
    .space 16

    .section .note.GNU-stack, "", @progbits
