/*
 * N64 stage0 payload.
 */
        .set    noreorder
        .set    noat
        .set    mips3

        .section .intvectors, "ax", @progbits
        .globl  n64_stage0_intvectors
n64_stage0_intvectors:
        b       n64_stage0_exception
        nop
        .space  0x400 - 16, 0

n64_stage0_exception:
        b       n64_stage0_exception
        nop

        .section .text.boot, "ax", @progbits
        .globl  _start
        .ent    _start
_start:
        lui     $sp, 0x8040
        addiu   $sp, $sp, -16

        jal     stage0_main
        nop

1:
        b       1b
        nop
        .end    _start

        .globl  stage0_jump_kernel
        .ent    stage0_jump_kernel
stage0_jump_kernel:
        dsll    $a0, $a0, 32
        dsra    $a0, $a0, 32
        jr      $a0
        nop
        .end    stage0_jump_kernel
