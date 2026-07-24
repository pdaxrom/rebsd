#include <machine/io.h>
#include <machine/machparam.h>

        .set    noreorder
#if defined(N64) || defined(MIPS3) || defined(MALTA64)
        .set    mips3
#else
        .set    mips32
#endif
        .set    noat

        .extern u_end
#ifndef N64_PREFLIGHT
        .extern mips_curuser
        .extern mips_init_process
#endif
        .extern main

        .section .startup,"ax",@progbits
        .globl  _start
        .globl  _reset_vector_
        .globl  mips_kernel_entry
        .ent    _start
_start:
_reset_vector_:
mips_kernel_entry:
        /*
         * Stage0 enters here in kernel mode. Keep interrupts and CP1 off
         * until the C machine layer explicitly enables them.
         */
        mfc0    $t0, $12
        li      $t1, ~(ST_IE | ST_EXL | ST_ERL | ST_KSU | ST_BEV | ST_FR | ST_CU1)
        and     $t0, $t0, $t1
        mtc0    $t0, $12
        nop
        nop
        nop

        /*
         * The bootstrap u area lives outside the ELF .bss segment. A software
         * restart through stage0 reloads the kernel but does not clear this
         * page, so clear it before using it as proc0's initial kernel stack.
         */
        la      $t0, u
        la      $t1, u_end
1:
        sw      $zero, 0($t0)
        addiu   $t0, $t0, 4
        bne     $t0, $t1, 1b
        nop

        la      $sp, u_end - 16
#ifndef N64_PREFLIGHT
        la      $t0, u
        la      $t1, mips_curuser
        sw      $t0, 0($t1)
#endif
        jal     main
        nop
1:
        b       1b
        nop
        .end    _start

#ifndef N64_PREFLIGHT
        .globl  mips_init_trampoline
        .ent    mips_init_trampoline
mips_init_trampoline:
        jal     mips_init_process
        nop
1:
        b       1b
        nop
        .end    mips_init_trampoline

        .globl  mips_user_enter
        .ent    mips_user_enter
mips_user_enter:
        mtc0    $a0, $14
        mfc0    $k0, $12
        li      $k1, ~(ST_EXL | ST_ERL | ST_KSU | ST_IM | ST_FR)
        and     $k0, $k0, $k1
#if defined(CI20)
        ori     $k0, $k0, ST_EXL | ST_IE | ST_IM2
#elif defined(MALTA) || defined(MALTA64) || defined(MALTA_ENABLED)
        ori     $k0, $k0, ST_EXL | ST_IE | ST_IM7
#else
        ori     $k0, $k0, ST_EXL | ST_IE | ST_IM2 | ST_IM7
#endif
#if defined(N64_USB_GDB) || defined(USBNET_ENABLED)
        /* Cartridge USB uses CART/IP3 for GDB and network traffic. */
        ori     $k0, $k0, ST_IM3
#endif
#ifdef N64_RESET_DUMP
        /* The reset button's pre-NMI is the crash-dump break-in source. */
        ori     $k0, $k0, ST_IM4
#endif
        ori     $k0, $k0, ST_KSU_USER
#if MIPS_FRAME_GPR64
        ori     $k0, $k0, ST_UX
#endif
        li      $k1, ST_CU1
        or      $k0, $k0, $k1
        mtc0    $k0, $12
        move    $sp, $a1
        nop
        nop
        nop
        eret
        nop
        .end    mips_user_enter

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

        la      $v0, mips_curuser
        sw      $a0, 0($v0)
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
#endif
