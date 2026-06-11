#include <machine/machparam.h>

#define SYS_read        3
#define SYS_write       4
#define SYS_open        5

#define O_RDWR          2

        .set    noreorder
        .set    mips3
        .set    noat

        .text
        .globl  _start
        .ent    _start
_start:
        la      $a0, console_path
        li      $a1, O_RDWR
        move    $a2, $zero
        syscall SYS_open
        nop
        nop
        move    $s0, $v0

        bltz    $s0, 2f
        nop

        move    $a0, $s0
        la      $a1, banner
        li      $a2, banner_end - banner
        syscall SYS_write
        nop
        nop

1:
        move    $a0, $s0
        la      $a1, input_byte
        li      $a2, 1
        syscall SYS_read
        nop
        nop

        blez    $v0, 1b
        nop

        move    $a0, $s0
        la      $a1, input_byte
        move    $a2, $v0
        syscall SYS_write
        nop
        nop
        b       1b
        nop

2:
        b       2b
        nop
        .end    _start

        .data
console_path:
        .ascii  "/dev/console\0"
banner:
        .ascii  "\nRetroBSD/N64 init\n"
        .ascii  "console echo ready\n"
banner_end:

        .bss
        .align  2
input_byte:
        .space  4
