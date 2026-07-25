    #include "context.h"

    .section .text, "ax", @progbits
    .code32

    .globl i386_user_enter
    .type i386_user_enter, @function
i386_user_enter:
    cli
    movl    4(%esp), %eax
    movl    8(%esp), %edx
    movw    $I386_USER_DATA_SELECTOR, %cx
    movw    %cx, %ds
    movw    %cx, %es
    movw    %cx, %fs
    movw    %cx, %gs
    pushl   $I386_USER_DATA_SELECTOR
    pushl   %edx
    pushl   $I386_EFLAGS_RESERVED
    pushl   $I386_USER_CODE_SELECTOR
    pushl   %eax
    iret
    .size i386_user_enter, . - i386_user_enter

    .globl i386_user_enter_exec
    .type i386_user_enter_exec, @function
i386_user_enter_exec:
    cli
    movw    $I386_USER_DATA_SELECTOR, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movl    4(%esp), %eax
    movl    8(%esp), %edi
    movl    12(%esp), %ebx
    movl    16(%esp), %ecx
    movl    20(%esp), %edx
    pushl   $I386_USER_DATA_SELECTOR
    pushl   %edi
    pushl   $(I386_EFLAGS_RESERVED | 0x00000200)
    pushl   $I386_USER_CODE_SELECTOR
    pushl   %eax
    iret
    .size i386_user_enter_exec, . - i386_user_enter_exec

    .section .note.GNU-stack, "", @progbits
