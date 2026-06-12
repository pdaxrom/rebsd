#include <machine/io.h>
#include <machine/machparam.h>

        .set    noreorder
        .set    mips3
        .set    noat

        .extern u_end
        .extern u0
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
        li      $t1, ~(ST_IE | ST_EXL | ST_ERL | ST_KSU | ST_BEV | ST_CU1)
        and     $t0, $t0, $t1
        mtc0    $t0, $12
        nop
        nop
        nop

        la      $sp, u_end - 16
        jal     main
        nop

        jal     n64_sync_user_icache
        nop

        la      $k0, USER_DATA_START
        mtc0    $k0, $14
        mfc0    $k0, $12
        li      $k1, ~(ST_EXL | ST_ERL | ST_KSU)
        and     $k0, $k0, $k1
        ori     $k0, $k0, ST_EXL | ST_IE | ST_IM2 | ST_IM7
        ori     $k0, $k0, ST_KSU_USER
        li      $k1, ST_CU1
        or      $k0, $k0, $k1
        mtc0    $k0, $12
        la      $sp, USER_DATA_END
        nop
        nop
        nop
        eret
        nop

1:
        b       1b
        nop
        .end    _start

        .weak   n64_sync_user_icache
        .ent    n64_sync_user_icache
n64_sync_user_icache:
        jr      $ra
        nop
        .end    n64_sync_user_icache

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
        mfc0    $t0, $12
        li      $t1, ~ST_IE
        and     $t0, $t0, $t1
        mtc0    $t0, $12
        nop
        nop
        nop

        la      $v0, u
        beq     $v0, $a0, 2f
        nop

        la      $a3, u_end
        la      $a0, u0

        lw      $v1, 0($v0)
        sw      $a0, 60($v1)

        move    $v1, $v0
1:
        lw      $t1, 0($v1)
        lw      $t0, 0($a0)
        sw      $t0, 0($v1)
        sw      $t1, 0($a0)
        lw      $t1, 4($v1)
        lw      $t0, 4($a0)
        sw      $t0, 4($v1)
        sw      $t1, 4($a0)
        lw      $t1, 8($v1)
        lw      $t0, 8($a0)
        sw      $t0, 8($v1)
        sw      $t1, 8($a0)
        lw      $t1, 12($v1)
        lw      $t0, 12($a0)
        sw      $t0, 12($v1)
        sw      $t1, 12($a0)
        addiu   $v1, $v1, 16
        bne     $a3, $v1, 1b
        addiu   $a0, $a0, 16

        lw      $v1, 0($v0)
        sw      $v0, 60($v1)
2:
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

        mfc0    $t0, $12
        ori     $t0, $t0, ST_IE
        mtc0    $t0, $12
        nop
        nop
        nop
        jr      $ra
        li      $v0, 1
        .end    longjmp
