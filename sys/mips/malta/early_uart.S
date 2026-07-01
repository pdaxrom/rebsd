/*
 * Minimal QEMU Malta early UART test.
 *
 * QEMU's big-endian Malta maps COM1 at physical 0x180003f8.  Use KSEG1 so
 * early writes are uncached and do not need any MMU setup.
 */

        .set    noreorder
        .set    mips32
        .set    noat

#define MALTA_UART0     0xb80003f8
#define UART_THR        0
#define UART_LSR        5
#define UART_LSR_THRE   0x20

        .section .startup,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        li      $sp, 0x807ff000
        la      $a0, malta_early_banner
        jal     malta_early_puts
        nop
1:
        b       1b
        nop
        .end    _start

        .ent    malta_early_puts
malta_early_puts:
        move    $t2, $ra
1:
        lbu     $a1, 0($a0)
        beqz    $a1, 2f
        addiu   $a0, $a0, 1
        jal     malta_early_putc
        move    $a2, $a0
        b       1b
        move    $a0, $a2
2:
        jr      $t2
        nop
        .end    malta_early_puts

        .ent    malta_early_putc
malta_early_putc:
        li      $t0, MALTA_UART0
1:
        lbu     $t1, UART_LSR($t0)
        andi    $t1, $t1, UART_LSR_THRE
        beqz    $t1, 1b
        nop
        sb      $a1, UART_THR($t0)
        jr      $ra
        nop
        .end    malta_early_putc

        .section .rodata,"a",@progbits
malta_early_banner:
        .ascii  "ReBSD Malta early UART ok\r\n"
        .byte   0
