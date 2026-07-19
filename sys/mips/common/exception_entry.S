#include <machine/io.h>
#include <machine/machparam.h>

        .set    noreorder
#if defined(N64) || defined(MIPS3) || defined(MALTA64)
        .set    mips3
#else
        .set    mips32
#endif
        .set    noat

        .extern mips_curuser
        .extern exception

        .section .exception,"ax",@progbits
        .globl  _mips_exception_vector
        .globl  _mips_exception_vector_end
        .globl  mips_exception_entry
        .globl  mips_exception_entry_end
        .globl  mips_exception_restore_start
        .globl  mips_exception_restore_end

        .ent    _mips_exception_vector
_mips_exception_vector:
        la      $k0, mips_exception_entry
        jr      $k0
        nop
_mips_exception_vector_end:
        .end    _mips_exception_vector

        .ent    mips_exception_entry
mips_exception_entry:
#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
        /*
         * USB/reset break-in and software breakpoints must not depend on the
         * interrupted kernel/u-area stack.  Preserve only the old sp in k1,
         * select the static debugger stack using k0, then save the complete
         * architectural frame through the normal path below.
         */
        move    $k1, $sp
#ifdef N64_USB_GDB
        mfc0    $k0, $13
        andi    $k0, $k0, CA_EXC_CODE
        xori    $k0, $k0, CA_Bp
        bnez    $k0, 6f
        nop
        la      $k0, n64_gdb_usb_active
        lw      $k0, 0($k0)
        bnez    $k0, 8f
        nop
        b       7f
        nop
6:
#endif
        mfc0    $k0, $13
        andi    $k0, $k0, CA_EXC_CODE
        bnez    $k0, 7f
        nop
        /*
         * Select the emergency stack only for an enabled pending source.
         * RESET/IP4 remains set in Cause while the button is held; looking at
         * raw Cause alone makes an unrelated timer interrupt use this stack.
         */
#ifdef N64_RESET_DUMP
        mfc0    $k0, $13
        andi    $k0, $k0, 0x1000
        beqz    $k0, 10f
        nop
        mfc0    $k0, $12
        andi    $k0, $k0, ST_IM4
        bnez    $k0, 8f
        nop
10:
#endif
#ifdef N64_USB_GDB
        mfc0    $k0, $13
        andi    $k0, $k0, 0x0800
        beqz    $k0, 7f
        nop
        mfc0    $k0, $12
        andi    $k0, $k0, ST_IM3
        bnez    $k0, 8f
        nop
#endif
7:
        mfc0    $k0, $12
        andi    $k0, $k0, ST_KSU
        xori    $k0, $k0, ST_KSU_USER
        bnez    $k0, 9f
        nop
        la      $sp, mips_curuser
        lw      $sp, 0($sp)
        addiu   $sp, $sp, USIZE
        b       9f
        nop
8:
        la      $sp, n64_gdb_emergency_stack_top
9:
        mfc0    $k0, $12
#else
        mfc0    $k0, $12
        andi    $k1, $k0, ST_KSU
        xori    $k1, $k1, ST_KSU_USER
        bnez    $k1, 1f
        move    $k1, $sp

        la      $sp, mips_curuser
        lw      $sp, 0($sp)
        addiu   $sp, $sp, USIZE
1:
#endif
        addiu   $sp, $sp, -FRAME_STACK_BYTES
        sw      $k0, (16+FRAME_STATUS*4)($sp)
#if MIPS_FRAME_GPR64
        sd      $k1, (16+(FRAME_SP-1)*4)($sp)
#else
        sw      $k1, (16+FRAME_SP*4)($sp)
#endif

#if MIPS_FRAME_GPR64
        sd      $1,  (16+(FRAME_R1-1)*4)($sp)
        sd      $2,  (16+(FRAME_R2-1)*4)($sp)
        sd      $3,  (16+(FRAME_R3-1)*4)($sp)
        sd      $4,  (16+(FRAME_R4-1)*4)($sp)
        sd      $5,  (16+(FRAME_R5-1)*4)($sp)
        sd      $6,  (16+(FRAME_R6-1)*4)($sp)
        sd      $7,  (16+(FRAME_R7-1)*4)($sp)
        sd      $8,  (16+(FRAME_R8-1)*4)($sp)
        sd      $9,  (16+(FRAME_R9-1)*4)($sp)
        sd      $10, (16+(FRAME_R10-1)*4)($sp)
        sd      $11, (16+(FRAME_R11-1)*4)($sp)
        sd      $12, (16+(FRAME_R12-1)*4)($sp)
        sd      $13, (16+(FRAME_R13-1)*4)($sp)
        sd      $14, (16+(FRAME_R14-1)*4)($sp)
        sd      $15, (16+(FRAME_R15-1)*4)($sp)
        sd      $16, (16+(FRAME_R16-1)*4)($sp)
        sd      $17, (16+(FRAME_R17-1)*4)($sp)
        sd      $18, (16+(FRAME_R18-1)*4)($sp)
        sd      $19, (16+(FRAME_R19-1)*4)($sp)
        sd      $20, (16+(FRAME_R20-1)*4)($sp)
        sd      $21, (16+(FRAME_R21-1)*4)($sp)
        sd      $22, (16+(FRAME_R22-1)*4)($sp)
        sd      $23, (16+(FRAME_R23-1)*4)($sp)
        sd      $24, (16+(FRAME_R24-1)*4)($sp)
        sd      $25, (16+(FRAME_R25-1)*4)($sp)
        sd      $28, (16+(FRAME_GP-1)*4)($sp)
        sd      $30, (16+(FRAME_FP-1)*4)($sp)
        sd      $31, (16+(FRAME_RA-1)*4)($sp)
#else
        sw      $1,  (16+FRAME_R1*4)($sp)
        sw      $2,  (16+FRAME_R2*4)($sp)
        sw      $3,  (16+FRAME_R3*4)($sp)
        sw      $4,  (16+FRAME_R4*4)($sp)
        sw      $5,  (16+FRAME_R5*4)($sp)
        sw      $6,  (16+FRAME_R6*4)($sp)
        sw      $7,  (16+FRAME_R7*4)($sp)
        sw      $8,  (16+FRAME_R8*4)($sp)
        sw      $9,  (16+FRAME_R9*4)($sp)
        sw      $10, (16+FRAME_R10*4)($sp)
        sw      $11, (16+FRAME_R11*4)($sp)
        sw      $12, (16+FRAME_R12*4)($sp)
        sw      $13, (16+FRAME_R13*4)($sp)
        sw      $14, (16+FRAME_R14*4)($sp)
        sw      $15, (16+FRAME_R15*4)($sp)
        sw      $16, (16+FRAME_R16*4)($sp)
        sw      $17, (16+FRAME_R17*4)($sp)
        sw      $18, (16+FRAME_R18*4)($sp)
        sw      $19, (16+FRAME_R19*4)($sp)
        sw      $20, (16+FRAME_R20*4)($sp)
        sw      $21, (16+FRAME_R21*4)($sp)
        sw      $22, (16+FRAME_R22*4)($sp)
        sw      $23, (16+FRAME_R23*4)($sp)
        sw      $24, (16+FRAME_R24*4)($sp)
        sw      $25, (16+FRAME_R25*4)($sp)
        sw      $28, (16+FRAME_GP*4)($sp)
        sw      $30, (16+FRAME_FP*4)($sp)
        sw      $31, (16+FRAME_RA*4)($sp)
#endif

        mflo    $k0
#if MIPS_FRAME_GPR64
        sd      $k0, (16+(FRAME_LO-1)*4)($sp)
#else
        sw      $k0, (16+FRAME_LO*4)($sp)
#endif
        mfhi    $k0
#if MIPS_FRAME_GPR64
        sd      $k0, (16+(FRAME_HI-1)*4)($sp)
#else
        sw      $k0, (16+FRAME_HI*4)($sp)
#endif
        mfc0    $k0, $14
        sw      $k0, (16+FRAME_PC*4)($sp)

        addiu   $a0, $sp, 16
        jal     exception
        nop

mips_exception_restore_start:
        /*
         * exception() may return with interrupts enabled after signal,
         * network, or scheduler work.  Keep the restore path atomic: it uses
         * k0/k1 as scratch registers, and nested interrupts are allowed to
         * clobber them.
         */
        mfc0    $k0, $12
        li      $k1, ~ST_IE
        and     $k0, $k0, $k1
        ori     $k0, $k0, ST_EXL
        mtc0    $k0, $12
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop

#if MIPS_FRAME_GPR64
        ld      $a0, (16+(FRAME_LO-1)*4)($sp)
        mtlo    $a0
        ld      $a0, (16+(FRAME_HI-1)*4)($sp)
        mthi    $a0

        ld      $1,  (16+(FRAME_R1-1)*4)($sp)
        ld      $2,  (16+(FRAME_R2-1)*4)($sp)
        ld      $3,  (16+(FRAME_R3-1)*4)($sp)
        ld      $4,  (16+(FRAME_R4-1)*4)($sp)
        ld      $5,  (16+(FRAME_R5-1)*4)($sp)
        ld      $6,  (16+(FRAME_R6-1)*4)($sp)
        ld      $7,  (16+(FRAME_R7-1)*4)($sp)
        ld      $8,  (16+(FRAME_R8-1)*4)($sp)
        ld      $9,  (16+(FRAME_R9-1)*4)($sp)
        ld      $10, (16+(FRAME_R10-1)*4)($sp)
        ld      $11, (16+(FRAME_R11-1)*4)($sp)
        ld      $12, (16+(FRAME_R12-1)*4)($sp)
        ld      $13, (16+(FRAME_R13-1)*4)($sp)
        ld      $14, (16+(FRAME_R14-1)*4)($sp)
        ld      $15, (16+(FRAME_R15-1)*4)($sp)
        ld      $16, (16+(FRAME_R16-1)*4)($sp)
        ld      $17, (16+(FRAME_R17-1)*4)($sp)
        ld      $18, (16+(FRAME_R18-1)*4)($sp)
        ld      $19, (16+(FRAME_R19-1)*4)($sp)
        ld      $20, (16+(FRAME_R20-1)*4)($sp)
        ld      $21, (16+(FRAME_R21-1)*4)($sp)
        ld      $22, (16+(FRAME_R22-1)*4)($sp)
        ld      $23, (16+(FRAME_R23-1)*4)($sp)
        ld      $24, (16+(FRAME_R24-1)*4)($sp)
        ld      $25, (16+(FRAME_R25-1)*4)($sp)
        ld      $28, (16+(FRAME_GP-1)*4)($sp)
        ld      $30, (16+(FRAME_FP-1)*4)($sp)
#else
        lw      $a0, (16+FRAME_LO*4)($sp)
        mtlo    $a0
        lw      $a0, (16+FRAME_HI*4)($sp)
        mthi    $a0

        lw      $1,  (16+FRAME_R1*4)($sp)
        lw      $2,  (16+FRAME_R2*4)($sp)
        lw      $3,  (16+FRAME_R3*4)($sp)
        lw      $4,  (16+FRAME_R4*4)($sp)
        lw      $5,  (16+FRAME_R5*4)($sp)
        lw      $6,  (16+FRAME_R6*4)($sp)
        lw      $7,  (16+FRAME_R7*4)($sp)
        lw      $8,  (16+FRAME_R8*4)($sp)
        lw      $9,  (16+FRAME_R9*4)($sp)
        lw      $10, (16+FRAME_R10*4)($sp)
        lw      $11, (16+FRAME_R11*4)($sp)
        lw      $12, (16+FRAME_R12*4)($sp)
        lw      $13, (16+FRAME_R13*4)($sp)
        lw      $14, (16+FRAME_R14*4)($sp)
        lw      $15, (16+FRAME_R15*4)($sp)
        lw      $16, (16+FRAME_R16*4)($sp)
        lw      $17, (16+FRAME_R17*4)($sp)
        lw      $18, (16+FRAME_R18*4)($sp)
        lw      $19, (16+FRAME_R19*4)($sp)
        lw      $20, (16+FRAME_R20*4)($sp)
        lw      $21, (16+FRAME_R21*4)($sp)
        lw      $22, (16+FRAME_R22*4)($sp)
        lw      $23, (16+FRAME_R23*4)($sp)
        lw      $24, (16+FRAME_R24*4)($sp)
        lw      $25, (16+FRAME_R25*4)($sp)
        lw      $28, (16+FRAME_GP*4)($sp)
        lw      $30, (16+FRAME_FP*4)($sp)
#endif

        lw      $31, (16+FRAME_STATUS*4)($sp)
        ori     $31, $31, ST_EXL
        mtc0    $31, $12
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop

        lw      $k0, (16+FRAME_PC*4)($sp)
        mtc0    $k0, $14
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop

#if MIPS_FRAME_GPR64
        ld      $31, (16+(FRAME_RA-1)*4)($sp)
        ld      $sp, (16+(FRAME_SP-1)*4)($sp)
#else
        lw      $31, (16+FRAME_RA*4)($sp)
        lw      $sp, (16+FRAME_SP*4)($sp)
#endif
        eret
        nop
mips_exception_restore_end:
mips_exception_entry_end:
        .end    mips_exception_entry

        .section .text,"ax",@progbits
        .globl  mips_fork_trampoline
        .ent    mips_fork_trampoline
mips_fork_trampoline:
        j       mips_exception_restore_start
        nop
        .end    mips_fork_trampoline

#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
        .section .bss,"aw",@nobits
        .align  4
        .globl  n64_gdb_emergency_stack
n64_gdb_emergency_stack:
        .space  8192
        .globl  n64_gdb_emergency_stack_top
n64_gdb_emergency_stack_top:
#endif
