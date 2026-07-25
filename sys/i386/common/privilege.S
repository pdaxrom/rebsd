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

    .section .note.GNU-stack, "", @progbits
