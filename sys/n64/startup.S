#include <machine/io.h>
#include <machine/machparam.h>

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

        /*
         * Initial user code copied to process 1 by init_main().
         */
        .section .text.icode,"ax",@progbits
        .globl  icode, icodeend, initflags
        .type   icode, @function
        .type   icodeend, @function
        .type   etcinit, @object
        .type   argv, @object
icode:
        la      $a0, USER_DATA_START
        move    $a1, $a0
        la      $t0, etcinit
        add     $a0, $t0
        la      $t0, argv
        add     $a1, $t0
        la      $t0, icode
        sub     $a0, $t0
        sub     $a1, $t0
        syscall 11
        move    $a0, $v0
        syscall 1
etcinit:
        .ascii  "/sbin/init\0"
        .align  2
initflags:
        .ascii  "-\0\0\0\0\0\0\0\0\0\0"
        .align  2
argv:
        .word   etcinit + 6 - icode + USER_DATA_START
        .word   initflags - icode + USER_DATA_START
        .word   0
icodeend:
        nop

        .globl  setjmp
        .ent    setjmp
setjmp:
        sw      $s0, (0 * 4)($a0)
        sw      $s1, (1 * 4)($a0)
        sw      $s2, (2 * 4)($a0)
        sw      $s3, (3 * 4)($a0)
        sw      $s4, (4 * 4)($a0)
        sw      $s5, (5 * 4)($a0)
        sw      $s6, (6 * 4)($a0)
        sw      $s7, (7 * 4)($a0)
        sw      $s8, (8 * 4)($a0)
        sw      $ra, (9 * 4)($a0)
        sw      $gp, (10 * 4)($a0)
        sw      $sp, (11 * 4)($a0)
        jr      $ra
        move    $v0, $zero
        .end    setjmp

        .globl  longjmp
        .ent    longjmp
longjmp:
        lw      $s0, (0 * 4)($a1)
        lw      $s1, (1 * 4)($a1)
        lw      $s2, (2 * 4)($a1)
        lw      $s3, (3 * 4)($a1)
        lw      $s4, (4 * 4)($a1)
        lw      $s5, (5 * 4)($a1)
        lw      $s6, (6 * 4)($a1)
        lw      $s7, (7 * 4)($a1)
        lw      $s8, (8 * 4)($a1)
        lw      $ra, (9 * 4)($a1)
        lw      $gp, (10 * 4)($a1)
        lw      $sp, (11 * 4)($a1)
        jr      $ra
        li      $v0, 1
        .end    longjmp
