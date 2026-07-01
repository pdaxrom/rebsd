#include <machine/machparam.h>

#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_execv       11
#define SYS_dup2        90

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

        la      $a0, console_path
        li      $a1, O_RDWR
        move    $a2, $zero
        syscall SYS_open
        nop
        nop

        li      $a0, 1
        li      $a1, 2
        syscall SYS_dup2
        nop
        nop

        li      $a0, 1
        la      $a1, banner
        li      $a2, banner_end - banner
        syscall SYS_write
        nop
        nop

        la      $t0, fpu_values
        lwc1    $f0, 0($t0)
        lwc1    $f2, 4($t0)
        add.s   $f4, $f0, $f2
        la      $t0, fpu_result
        swc1    $f4, 0($t0)
        lw      $t0, 0($t0)
        li      $t1, 0x40700000
        bne     $t0, $t1, 3f
        nop

        li      $a0, 1
        la      $a1, fpu_ok
        li      $a2, fpu_ok_end - fpu_ok
        syscall SYS_write
        nop
        nop
        b       1f
        nop

3:
        li      $a0, 1
        la      $a1, fpu_fail
        li      $a2, fpu_fail_end - fpu_fail
        syscall SYS_write
        nop
        nop

1:
        la      $a0, shell_path
        la      $a1, shell_argv
        syscall SYS_execv
        nop
        nop

        li      $a0, 2
        la      $a1, exec_fail
        li      $a2, exec_fail_end - exec_fail
        syscall SYS_write
        nop
        nop

4:
        move    $a0, $s0
        la      $a1, input_byte
        li      $a2, 1
        syscall SYS_read
        nop
        nop

        blez    $v0, 4b
        nop

        move    $a0, $s0
        la      $a1, input_byte
        move    $a2, $v0
        syscall SYS_write
        nop
        nop
        b       4b
        nop

2:
        b       2b
        nop
        .end    _start

        .data
console_path:
        .ascii  "/dev/console\0"
shell_path:
        .ascii  "/bin/sh\0"
shell_arg0:
        .ascii  "sh\0"
shell_arg1:
        .ascii  "-i\0"
        .align  2
shell_argv:
        .word   shell_arg0
        .word   shell_arg1
        .word   0
banner:
        .ascii  "\nReBSD/N64 init\n"
banner_end:
fpu_ok:
        .ascii  "fpu ok\n"
fpu_ok_end:
fpu_fail:
        .ascii  "fpu failed\n"
fpu_fail_end:
exec_fail:
        .ascii  "exec /bin/sh failed\n"
        .ascii  "console echo ready\n"
exec_fail_end:

        .align  2
fpu_values:
        .word   0x3fc00000
        .word   0x40100000
fpu_result:
        .word   0

        .bss
        .align  2
input_byte:
        .space  4
