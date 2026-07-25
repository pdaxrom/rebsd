/*
 * Early protected-mode exception and IRQ entry stubs.
 *
 * Every path presents the same vector/error pair to the common entry.
 * Exceptions for which the CPU already pushed an error code only add the
 * vector; all other paths add a synthetic zero error code first.
 */

    .section .text, "ax", @progbits
    .code32

    .macro I386_VECTOR_NO_ERROR number
    .p2align 4
i386_vector_\number:
    pushl   $0
    pushl   $\number
    jmp     i386_interrupt_common
    .endm

    .macro I386_VECTOR_ERROR number
    .p2align 4
i386_vector_\number:
    pushl   $\number
    jmp     i386_interrupt_common
    .endm

    I386_VECTOR_NO_ERROR 0
    I386_VECTOR_NO_ERROR 1
    I386_VECTOR_NO_ERROR 2
    I386_VECTOR_NO_ERROR 3
    I386_VECTOR_NO_ERROR 4
    I386_VECTOR_NO_ERROR 5
    I386_VECTOR_NO_ERROR 6
    I386_VECTOR_NO_ERROR 7
    I386_VECTOR_ERROR    8
    I386_VECTOR_NO_ERROR 9
    I386_VECTOR_ERROR    10
    I386_VECTOR_ERROR    11
    I386_VECTOR_ERROR    12
    I386_VECTOR_ERROR    13
    I386_VECTOR_ERROR    14
    I386_VECTOR_NO_ERROR 15
    I386_VECTOR_NO_ERROR 16
    I386_VECTOR_ERROR    17
    I386_VECTOR_NO_ERROR 18
    I386_VECTOR_NO_ERROR 19
    I386_VECTOR_NO_ERROR 20
    I386_VECTOR_NO_ERROR 21
    I386_VECTOR_NO_ERROR 22
    I386_VECTOR_NO_ERROR 23
    I386_VECTOR_NO_ERROR 24
    I386_VECTOR_NO_ERROR 25
    I386_VECTOR_NO_ERROR 26
    I386_VECTOR_NO_ERROR 27
    I386_VECTOR_NO_ERROR 28
    I386_VECTOR_NO_ERROR 29
    I386_VECTOR_NO_ERROR 30
    I386_VECTOR_NO_ERROR 31

    I386_VECTOR_NO_ERROR 32
    I386_VECTOR_NO_ERROR 33
    I386_VECTOR_NO_ERROR 34
    I386_VECTOR_NO_ERROR 35
    I386_VECTOR_NO_ERROR 36
    I386_VECTOR_NO_ERROR 37
    I386_VECTOR_NO_ERROR 38
    I386_VECTOR_NO_ERROR 39
    I386_VECTOR_NO_ERROR 40
    I386_VECTOR_NO_ERROR 41
    I386_VECTOR_NO_ERROR 42
    I386_VECTOR_NO_ERROR 43
    I386_VECTOR_NO_ERROR 44
    I386_VECTOR_NO_ERROR 45
    I386_VECTOR_NO_ERROR 46
    I386_VECTOR_NO_ERROR 47
    I386_VECTOR_NO_ERROR 48

    .globl i386_vector_128
    I386_VECTOR_NO_ERROR 128

    .p2align 4
i386_unhandled_vector:
    pushl   $0
    pushl   $255
    jmp     i386_interrupt_common

    .p2align 4
i386_interrupt_common:
    cld
    pushal

    xorl    %eax, %eax
    movw    %ds, %ax
    pushl   %eax
    movw    %es, %ax
    pushl   %eax
    movw    %fs, %ax
    pushl   %eax
    movw    %gs, %ax
    pushl   %eax

    movw    $0x0010, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    pushl   %esp
    call    i386_interrupt_dispatch
    addl    $4, %esp

    pushl   %esp
    call    i386_user_return
    addl    $4, %esp

    .globl i386_trap_return
    .type i386_trap_return, @function
i386_trap_return:
    popl    %eax
    movw    %ax, %gs
    popl    %eax
    movw    %ax, %fs
    popl    %eax
    movw    %ax, %es
    popl    %eax
    movw    %ax, %ds

    popal
    addl    $8, %esp
    iret
    .size i386_trap_return, . - i386_trap_return

    .section .rodata, "a", @progbits
    .p2align 2
    .globl i386_vector_table
    .type i386_vector_table, @object
i386_vector_table:
    .long i386_vector_0
    .long i386_vector_1
    .long i386_vector_2
    .long i386_vector_3
    .long i386_vector_4
    .long i386_vector_5
    .long i386_vector_6
    .long i386_vector_7
    .long i386_vector_8
    .long i386_vector_9
    .long i386_vector_10
    .long i386_vector_11
    .long i386_vector_12
    .long i386_vector_13
    .long i386_vector_14
    .long i386_vector_15
    .long i386_vector_16
    .long i386_vector_17
    .long i386_vector_18
    .long i386_vector_19
    .long i386_vector_20
    .long i386_vector_21
    .long i386_vector_22
    .long i386_vector_23
    .long i386_vector_24
    .long i386_vector_25
    .long i386_vector_26
    .long i386_vector_27
    .long i386_vector_28
    .long i386_vector_29
    .long i386_vector_30
    .long i386_vector_31
    .long i386_vector_32
    .long i386_vector_33
    .long i386_vector_34
    .long i386_vector_35
    .long i386_vector_36
    .long i386_vector_37
    .long i386_vector_38
    .long i386_vector_39
    .long i386_vector_40
    .long i386_vector_41
    .long i386_vector_42
    .long i386_vector_43
    .long i386_vector_44
    .long i386_vector_45
    .long i386_vector_46
    .long i386_vector_47
    .long i386_vector_48
    .size i386_vector_table, . - i386_vector_table

    .globl i386_unhandled_vector_entry
    .type i386_unhandled_vector_entry, @object
i386_unhandled_vector_entry:
    .long i386_unhandled_vector
    .size i386_unhandled_vector_entry, . - i386_unhandled_vector_entry

    .section .note.GNU-stack, "", @progbits
