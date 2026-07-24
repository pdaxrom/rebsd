#include <machine/io.h>
#include <machine/machparam.h>
        /*
         * PCC's assembler preprocessor can remove a conditional block without
         * preserving a statement boundary.  Semicolons immediately before or
         * inside CPP alternatives below deliberately keep adjacent MIPS
         * instructions separate in every configuration.
         */

        .set    noreorder
#if defined(N64) || defined(MIPS3) || defined(MALTA64)
        .set    mips3
#else
        .set    mips32
#endif
        .set    noat

        .extern mips_curuser
        .extern exception
        .extern mips_pmap_fast_directory
        .extern mips_pmap_fast_refills
        .extern mips_pmap_fast_scratch
        .extern mips_pmap_fast_last_hash
        .extern mips_pmap_fast_last_epc
        .extern mips_pmap_fast_last_vaddr
        .extern mips_pmap_fast_repeat

        .section .exception,"ax",@progbits
        .globl  _mips_exception_vector
        .globl  _mips_exception_vector_end
        .globl  _mips_tlb_refill_vector
        .globl  _mips_tlb_refill_vector_end
        .globl  mips_tlb_refill_entry
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
        .end    _mips_exception_vector ;

        /*
         * Resident user pages normally miss only because Random replaced the
         * hardware entry.  Refill those mappings directly from the active
         * two-level pmap instead of saving a complete frame and entering the
         * C trap handler.  A page which has not yet been referenced/modified,
         * is absent, or fails software protection still takes the full path.
         * Only k0/k1, one per-CPU scratch word, and CP0 EntryLo scratch state
         * are touched, so the interrupted register file remains intact.
         */
        .ent    _mips_tlb_refill_vector
_mips_tlb_refill_vector:
        la      $k0, mips_tlb_refill_entry
        jr      $k0
        nop
_mips_tlb_refill_vector_end:
        .end    _mips_tlb_refill_vector

        .ent    mips_tlb_refill_entry
mips_tlb_refill_entry:
        /*
         * Record consecutive identical refill sites.  This uses only k0/k1
         * and cached kernel globals, so the interrupted GPR file is intact
         * and no console, lock, allocator, or VM policy is involved.
         */
        mfc0    $k0, $14                /* EPC */
        mfc0    $k1, $8                 /* BadVAddr */
        xor     $k0, $k0, $k1
        la      $k1, mips_pmap_fast_last_hash
        lw      $k1, 0($k1)
        bne     $k0, $k1, mips_tlb_refill_new_site
        nop
        la      $k0, mips_pmap_fast_repeat
        lw      $k1, 0($k0)
        addiu   $k1, $k1, 1
        bnez    $k1, mips_tlb_refill_repeat_ready
        nop
        addiu   $k1, $zero, -1         /* saturate on wrap */
mips_tlb_refill_repeat_ready:
        sw      $k1, 0($k0)
        b       mips_tlb_refill_site_recorded
        nop
mips_tlb_refill_new_site:
        la      $k1, mips_pmap_fast_last_hash
        sw      $k0, 0($k1)
        addiu   $k0, $zero, 1
        la      $k1, mips_pmap_fast_repeat
        sw      $k0, 0($k1)
mips_tlb_refill_site_recorded:
        mfc0    $k0, $14
        la      $k1, mips_pmap_fast_last_epc
        sw      $k0, 0($k1)
        mfc0    $k0, $8
        la      $k1, mips_pmap_fast_last_vaddr
        sw      $k0, 0($k1)

        mfc0    $k0, $8                 /* BadVAddr */
        bltz    $k0, mips_tlb_refill_slow
        nop

        srl     $k1, $k0, 22
        sll     $k1, $k1, 2
        la      $k0, mips_pmap_fast_directory
        lw      $k0, 0($k0)
        beqz    $k0, mips_tlb_refill_slow
        nop
        addu    $k0, $k0, $k1
        lw      $k0, 0($k0)
        andi    $k1, $k0, 0x0001       /* PMAP_PTE_PRESENT */
        beqz    $k1, mips_tlb_refill_slow
        nop
        srl     $k0, $k0, 12
        sll     $k0, $k0, 12
        lui     $k1, 0x8000            /* cached direct map */
        or      $k0, $k0, $k1

        mfc0    $k1, $8
        srl     $k1, $k1, 10
        andi    $k1, $k1, 0x0ff8       /* even PTE byte offset */
        addu    $k0, $k0, $k1
        la      $k1, mips_pmap_fast_scratch
        sw      $k0, 0($k1)            /* save pair PTE pointer */

        mfc0    $k1, $8
        andi    $k1, $k1, 0x1000
        la      $k0, mips_pmap_fast_scratch
        lw      $k0, 0($k0)
        beqz    $k1, 1f
        nop
        addiu   $k0, $k0, 4
1:
        lw      $k1, 0($k0)            /* faulting software PTE */
        mfc0    $k0, $13               /* Cause */
        andi    $k0, $k0, CA_EXC_CODE
        addiu   $k0, $k0, -CA_TLBS
        beqz    $k0, 2f
        nop
        mfc0    $k0, $13
        andi    $k0, $k0, CA_EXC_CODE
        addiu   $k0, $k0, -CA_TLBL
        bnez    $k0, mips_tlb_refill_slow
        nop
        andi    $k0, $k1, 0x0013       /* present, read, referenced */
        xori    $k0, $k0, 0x0013
        bnez    $k0, mips_tlb_refill_slow
        nop
        b       3f
        nop
2:
        andi    $k0, $k1, 0x0035       /* present, write, ref, modified */
        xori    $k0, $k0, 0x0035
        bnez    $k0, mips_tlb_refill_slow
        nop

        /* Convert the even software PTE to EntryLo0. */
3:
        la      $k0, mips_pmap_fast_scratch
        lw      $k0, 0($k0)
        lw      $k1, 0($k0)
        andi    $k0, $k1, 0x0011       /* present and referenced */
        xori    $k0, $k0, 0x0011
        bnez    $k0, 6f
        nop
        mtc0    $k1, $2                /* retain raw PTE temporarily */
        srl     $k1, $k1, 12
        sll     $k1, $k1, 6
        mfc0    $k0, $2
        andi    $k0, $k0, 0x0040       /* PMAP_PTE_UNCACHED */
        beqz    $k0, 4f
        nop
        ori     $k1, $k1, 0x0012       /* uncached, valid */
        b       5f
        nop
4:
        ori     $k1, $k1, 0x001a       /* cached, valid */
5:
        mfc0    $k0, $2
        andi    $k0, $k0, 0x0024       /* write and modified */
        xori    $k0, $k0, 0x0024
        bnez    $k0, 7f
        nop
        ori     $k1, $k1, 0x0004       /* dirty */
        b       7f
        nop
6:
        move    $k1, $zero
7:
        mtc0    $k1, $2

        /* Convert the odd software PTE to EntryLo1. */
        la      $k0, mips_pmap_fast_scratch
        lw      $k0, 0($k0)
        lw      $k1, 4($k0)
        andi    $k0, $k1, 0x0011
        xori    $k0, $k0, 0x0011
        bnez    $k0, 10f
        nop
        mtc0    $k1, $3
        srl     $k1, $k1, 12
        sll     $k1, $k1, 6
        mfc0    $k0, $3
        andi    $k0, $k0, 0x0040
        beqz    $k0, 8f
        nop
        ori     $k1, $k1, 0x0012
        b       9f
        nop
8:
        ori     $k1, $k1, 0x001a
9:
        mfc0    $k0, $3
        andi    $k0, $k0, 0x0024
        xori    $k0, $k0, 0x0024
        bnez    $k0, 11f
        nop
        ori     $k1, $k1, 0x0004
        b       11f
        nop
10:
        move    $k1, $zero
11:
        mtc0    $k1, $3

        mtc0    $zero, $5              /* 4 KiB PageMask */
        nop
        nop
        nop
        tlbp
        nop
        nop
        nop
        mfc0    $k0, $0
        bltz    $k0, 12f
        nop
        tlbwi
        b       13f
        nop
12:
        tlbwr
13:
        nop
        nop
        nop
        la      $k0, mips_pmap_fast_refills
        lw      $k1, 0($k0)
        addiu   $k1, $k1, 1
        bnez    $k1, 14f
        nop
        addiu   $k1, $zero, -1         /* saturate on wrap */
14:
        sw      $k1, 0($k0)
        eret
        nop

mips_tlb_refill_slow:
        la      $k0, mips_exception_entry
        jr      $k0
        nop
        .end    mips_tlb_refill_entry

        .ent    mips_exception_entry
mips_exception_entry:
#if defined(N64_USB_GDB) || defined(N64_RESET_DUMP)
        /*
         * USB/reset break-in and software breakpoints must not depend on the
         * interrupted kernel/u-area stack.  Preserve only the old sp in k1,
         * select the static debugger stack using k0, then save the complete
         * architectural frame through the normal path below.
         */
        move    $k1, $sp ;
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
        mfc0    $k0, $12 ;
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
        sd      $k1, (16+(FRAME_SP-1)*4)($sp) ;
#else
        sw      $k1, (16+FRAME_SP*4)($sp) ;
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
        sd      $31, (16+(FRAME_RA-1)*4)($sp) ;
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
        sw      $31, (16+FRAME_RA*4)($sp) ;
#endif

        mflo    $k0
#if MIPS_FRAME_GPR64
        sd      $k0, (16+(FRAME_LO-1)*4)($sp) ;
#else
        sw      $k0, (16+FRAME_LO*4)($sp) ;
#endif
        mfhi    $k0
#if MIPS_FRAME_GPR64
        sd      $k0, (16+(FRAME_HI-1)*4)($sp) ;
#else
        sw      $k0, (16+FRAME_HI*4)($sp) ;
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
        ld      $30, (16+(FRAME_FP-1)*4)($sp) ;
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
        lw      $30, (16+FRAME_FP*4)($sp) ;
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
        ld      $sp, (16+(FRAME_SP-1)*4)($sp) ;
#else
        lw      $31, (16+FRAME_RA*4)($sp)
        lw      $sp, (16+FRAME_SP*4)($sp) ;
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
