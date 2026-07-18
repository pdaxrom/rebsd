        .set    noreorder
        .set    mips3

#ifdef MALTA_N64_8M_PROFILE
        .equ    MALTA64_KERNEL_KSEG0, 0xffffffff80001000
        .equ    MALTA64_KERNEL_KSEG1, 0xffffffffa0001000
        .equ    MALTA64_STAGE0_STACK, 0xffffffff8037f000
#else
        .equ    MALTA64_KERNEL_KSEG0, 0xffffffff80100000
        .equ    MALTA64_KERNEL_KSEG1, 0xffffffffa0100000
        .equ    MALTA64_STAGE0_STACK, 0xffffffff8007f000
#endif

        .extern __malta64_kernel_blob_start
        .extern __malta64_kernel_blob_end

        .section .startup,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        dli     $sp, MALTA64_STAGE0_STACK

        dla     $a0, malta64_stage0_banner
        jal     malta64_stage0_puts
        nop

        dla     $t0, __malta64_kernel_blob_start
        dla     $t1, __malta64_kernel_blob_end
        dsubu   $t2, $t1, $t0
        dli     $t3, MALTA64_KERNEL_KSEG1
        daddu   $t3, $t3, $t2

        beqz    $t2, 2f
        nop
1:
        daddiu  $t1, $t1, -1
        daddiu  $t3, $t3, -1
        lbu     $12, 0($t1)
        daddiu  $t2, $t2, -1
        bnez    $t2, 1b
        sb      $12, 0($t3)

2:
        sync
        dli     $t0, MALTA64_KERNEL_KSEG0
        jr      $t0
        nop
        .end    _start

        .ent    malta64_stage0_puts
malta64_stage0_puts:
        move    $14, $ra
1:
        lbu     $a1, 0($a0)
        beqz    $a1, 2f
        daddiu  $a0, $a0, 1
        jal     malta64_stage0_putc
        move    $a2, $a0
        b       1b
        move    $a0, $a2
2:
        jr      $14
        nop
        .end    malta64_stage0_puts

        .ent    malta64_stage0_putc
malta64_stage0_putc:
        dli     $t0, 0xffffffffb80003f8
1:
        lbu     $t1, 5($t0)
        andi    $t1, $t1, 0x20
        beqz    $t1, 1b
        nop
        sb      $a1, 0($t0)
        jr      $ra
        nop
        .end    malta64_stage0_putc

malta64_stage0_banner:
        .ascii  "ReBSD Malta64 stage0\r\n\0"
