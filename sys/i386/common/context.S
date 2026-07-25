    .section .text, "ax", @progbits
    .code32

    .equ I386_LABEL_EBX,       0
    .equ I386_LABEL_ESI,       4
    .equ I386_LABEL_EDI,       8
    .equ I386_LABEL_EBP,       12
    .equ I386_LABEL_ESP,       16
    .equ I386_LABEL_EIP,       20
    .equ I386_LABEL_EFLAGS,    24

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

    /*
     * Exercise the public save/restore entry points without relying on C
     * compiler allocation of callee-saved registers.
     */
    .globl i386_context_register_selftest
    .type i386_context_register_selftest, @function
i386_context_register_selftest:
    pushl   %ebp
    movl    %esp, %ebp
    pushl   %ebx
    pushl   %esi
    pushl   %edi

    movl    $0x13579bdf, %ebx
    movl    $0x2468ace0, %esi
    movl    $0x55aa33cc, %edi
    pushl   8(%ebp)
    call    setjmp
    addl    $4, %esp
    testl   %eax, %eax
    jnz     1f

    movl    $0xdeadbeef, %ebx
    movl    $0xc001d00d, %esi
    movl    $0x0badf00d, %edi
    pushl   8(%ebp)
    pushl   12(%ebp)
    call    longjmp
    ud2

1:
    cmpl    $0x13579bdf, %ebx
    jne     2f
    cmpl    $0x2468ace0, %esi
    jne     2f
    cmpl    $0x55aa33cc, %edi
    jne     2f
    xorl    %eax, %eax
    jmp     3f
2:
    movl    $1, %eax
3:
    popl    %edi
    popl    %esi
    popl    %ebx
    leave
    ret
    .size i386_context_register_selftest, . - i386_context_register_selftest

    .section .note.GNU-stack, "", @progbits
