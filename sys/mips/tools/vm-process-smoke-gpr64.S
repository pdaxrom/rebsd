/* Exercise preservation of a live MIPS III GPR across timer interrupts. */

#include <syscall.h>

        .set    noreorder
        .set    mips3

        .text
        .globl  smoke_gpr64_exception_frame_asm
        .ent    smoke_gpr64_exception_frame_asm
smoke_gpr64_exception_frame_asm:
        addiu   $sp, $sp, -16
        sd      $16, 0($sp)
        ld      $16, 0($4)
        move    $8, $6
1:
        addiu   $8, $8, -1
        bnez    $8, 1b
        nop
        sd      $16, 0($5)
        ld      $16, 0($sp)
        jr      $31
        addiu   $sp, $sp, 16
        .end    smoke_gpr64_exception_frame_asm

        .globl  smoke_gpr64_signal_frame_asm
        .ent    smoke_gpr64_signal_frame_asm
smoke_gpr64_signal_frame_asm:
        addiu   $sp, $sp, -16
        sd      $16, 0($sp)
        sw      $5, 8($sp)
        ld      $16, 0($4)
        move    $4, $6
        move    $5, $7
        syscall SYS_kill
        nop
        nop
        lw      $8, 8($sp)
        sd      $16, 0($8)
        ld      $16, 0($sp)
        jr      $31
        addiu   $sp, $sp, 16
        .end    smoke_gpr64_signal_frame_asm
