        .set    noreorder
        .set    mips3

        .section .startup,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        dli     $sp, 0xffffffff807ff000
        dla     $a0, malta64_early_banner
        jal     malta64_early_puts
        nop
1:
        b       1b
        nop
        .end    _start

        .ent    malta64_early_puts
malta64_early_puts:
        move    $t2, $ra
1:
        lbu     $a1, 0($a0)
        beqz    $a1, 2f
        daddiu  $a0, $a0, 1
        jal     malta64_early_putc
        move    $a2, $a0
        b       1b
        move    $a0, $a2
2:
        jr      $t2
        nop
        .end    malta64_early_puts

        .ent    malta64_early_putc
malta64_early_putc:
        dli     $t0, 0xffffffffb80003f8
1:
        lbu     $t1, 5($t0)
        andi    $t1, $t1, 0x20
        beqz    $t1, 1b
        nop
        sb      $a1, 0($t0)
        jr      $ra
        nop
        .end    malta64_early_putc

malta64_early_banner:
        .ascii  "ReBSD Malta64 early UART ok\r\n\0"
