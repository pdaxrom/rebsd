#include <machine/io.h>

        .set    noreorder
        .set    mips3
        .set    noat

        .extern u_end
        .extern main

        .section .startup,"ax",@progbits
        .globl  _start
        .globl  _reset_vector_
        .globl  n64_kernel_entry
        .ent    _start
_start:
_reset_vector_:
n64_kernel_entry:
        /*
         * Stage0 enters here in kernel mode. Keep interrupts and CP1 off
         * until the C machine layer explicitly enables them.
         */
        mfc0    $t0, $12
        li      $t1, ~(ST_IE | ST_CU1)
        and     $t0, $t0, $t1
        mtc0    $t0, $12
        nop
        nop
        nop

        la      $sp, u_end - 16
        jal     main
        nop

1:
        b       1b
        nop
        .end    _start

        .section .exception,"ax",@progbits
        .globl  _n64_exception_vector
        .ent    _n64_exception_vector
_n64_exception_vector:
        /*
         * Placeholder copied to 0x80000180 by the future startup path.
         */
        b       _n64_exception_vector
        nop
        .end    _n64_exception_vector
