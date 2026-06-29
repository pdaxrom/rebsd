#include <machine/io.h>

        .set    noreorder
#ifdef N64
        .set    mips3
#else
        .set    mips32
#endif
        .set    noat

        .extern u_end
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
        mfc0    $k0, $12
        andi    $k1, $k0, ST_KSU
        xori    $k1, $k1, ST_KSU_USER
        bnez    $k1, 1f
        move    $k1, $sp

        la      $sp, u_end
1:
        addiu   $sp, $sp, -16-FRAME_WORDS*4
        sw      $k0, (16+FRAME_STATUS*4)($sp)
        sw      $k1, (16+FRAME_SP*4)($sp)

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

        mflo    $k0
        sw      $k0, (16+FRAME_LO*4)($sp)
        mfhi    $k0
        sw      $k0, (16+FRAME_HI*4)($sp)
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

        lw      $31, (16+FRAME_RA*4)($sp)
        lw      $sp, (16+FRAME_SP*4)($sp)
        eret
        nop
mips_exception_restore_end:
mips_exception_entry_end:
        .end    mips_exception_entry
