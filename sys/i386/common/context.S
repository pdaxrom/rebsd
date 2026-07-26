#include "context.h"
#include <machine/layout.h>

    .section .text, "ax", @progbits
    .code32

    .globl setjmp
    .type setjmp, @function
setjmp:
    movl    4(%esp), %eax
    movl    %ebx, I386_LABEL_EBX(%eax)
    movl    %esi, I386_LABEL_ESI(%eax)
    movl    %edi, I386_LABEL_EDI(%eax)
    movl    %ebp, I386_LABEL_EBP(%eax)
    leal    4(%esp), %edx
    movl    %edx, I386_LABEL_ESP(%eax)
    movl    (%esp), %edx
    movl    %edx, I386_LABEL_EIP(%eax)
    pushfl
    popl    %edx
    movl    %edx, I386_LABEL_EFLAGS(%eax)
    xorl    %eax, %eax
    ret
    .size setjmp, . - setjmp

    .globl longjmp
    .type longjmp, @function
longjmp:
    cli
    movl    4(%esp), %eax
    addl    $I386_UAREA_SIZE, %eax
    pushl   %eax
    call    i386_tss_set_kernel_stack
    addl    $4, %esp
    movl    4(%esp), %edx
    movl    %edx, md_curuser
    movl    8(%esp), %edx
    movl    I386_LABEL_EIP(%edx), %ecx
    movl    I386_LABEL_EFLAGS(%edx), %eax
    movl    I386_LABEL_EBX(%edx), %ebx
    movl    I386_LABEL_ESI(%edx), %esi
    movl    I386_LABEL_EDI(%edx), %edi
    movl    I386_LABEL_EBP(%edx), %ebp
    movl    I386_LABEL_ESP(%edx), %esp
    pushl   %eax
    popfl
    movl    $1, %eax
    jmp     *%ecx
    .size longjmp, . - longjmp

    .globl i386_fork_trampoline
    .type i386_fork_trampoline, @function
i386_fork_trampoline:
    jmp     i386_trap_return
    .size i386_fork_trampoline, . - i386_fork_trampoline

    .globl i386_init_trampoline
    .type i386_init_trampoline, @function
i386_init_trampoline:
    call    init_process
1:
    cli
    hlt
    jmp     1b
    .size i386_init_trampoline, . - i386_init_trampoline

    .globl i386_context_enter
    .type i386_context_enter, @function
i386_context_enter:
    cli
    movl    4(%esp), %eax
    addl    $I386_UAREA_SIZE, %eax
    movl    8(%esp), %edx
    movl    %eax, %esp
    pushl   $0
    jmp     *%edx
    .size i386_context_enter, . - i386_context_enter

    /*
     * Initial user code copied to process 1 by the common init_process().
     */
    .section .text.icode, "ax", @progbits
    .globl icode, icodeend, initflags
    .type icode, @function
    .type icodeend, @function
icode:
    movl    $(etcinit - icode + I386_USER_VADDR_START), %ebx
    movl    $(argv - icode + I386_USER_VADDR_START), %ecx
    movl    $11, %eax
    int     $0x80
    movl    %eax, %ebx
    movl    $1, %eax
    int     $0x80
1:
    jmp     1b
etcinit:
    .ascii  "/sbin/init\0"
    .p2align 2
initflags:
    .ascii  "-\0\0\0\0\0\0\0\0\0\0"
    .p2align 2
argv:
    .long   etcinit + 6 - icode + I386_USER_VADDR_START
    .long   initflags - icode + I386_USER_VADDR_START
    .long   0
icodeend:
    nop

    .section .note.GNU-stack, "", @progbits
