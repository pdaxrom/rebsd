#include <machine/fpu.h>

        .set    noreorder
        .set    mips3
        .set    noat

        .text
        .globl  n64_fpu_save
        .ent    n64_fpu_save
n64_fpu_save:
        swc1    $f0,   0($a0)
        swc1    $f1,   4($a0)
        swc1    $f2,   8($a0)
        swc1    $f3,  12($a0)
        swc1    $f4,  16($a0)
        swc1    $f5,  20($a0)
        swc1    $f6,  24($a0)
        swc1    $f7,  28($a0)
        swc1    $f8,  32($a0)
        swc1    $f9,  36($a0)
        swc1    $f10, 40($a0)
        swc1    $f11, 44($a0)
        swc1    $f12, 48($a0)
        swc1    $f13, 52($a0)
        swc1    $f14, 56($a0)
        swc1    $f15, 60($a0)
        swc1    $f16, 64($a0)
        swc1    $f17, 68($a0)
        swc1    $f18, 72($a0)
        swc1    $f19, 76($a0)
        swc1    $f20, 80($a0)
        swc1    $f21, 84($a0)
        swc1    $f22, 88($a0)
        swc1    $f23, 92($a0)
        swc1    $f24, 96($a0)
        swc1    $f25,100($a0)
        swc1    $f26,104($a0)
        swc1    $f27,108($a0)
        swc1    $f28,112($a0)
        swc1    $f29,116($a0)
        swc1    $f30,120($a0)
        swc1    $f31,124($a0)
        cfc1    $t0, $31
        sw      $t0, 128($a0)
        jr      $ra
        nop
        .end    n64_fpu_save

        .globl  n64_fpu_restore
        .ent    n64_fpu_restore
n64_fpu_restore:
        lw      $t0, 128($a0)
        ctc1    $t0, $31
        lwc1    $f0,   0($a0)
        lwc1    $f1,   4($a0)
        lwc1    $f2,   8($a0)
        lwc1    $f3,  12($a0)
        lwc1    $f4,  16($a0)
        lwc1    $f5,  20($a0)
        lwc1    $f6,  24($a0)
        lwc1    $f7,  28($a0)
        lwc1    $f8,  32($a0)
        lwc1    $f9,  36($a0)
        lwc1    $f10, 40($a0)
        lwc1    $f11, 44($a0)
        lwc1    $f12, 48($a0)
        lwc1    $f13, 52($a0)
        lwc1    $f14, 56($a0)
        lwc1    $f15, 60($a0)
        lwc1    $f16, 64($a0)
        lwc1    $f17, 68($a0)
        lwc1    $f18, 72($a0)
        lwc1    $f19, 76($a0)
        lwc1    $f20, 80($a0)
        lwc1    $f21, 84($a0)
        lwc1    $f22, 88($a0)
        lwc1    $f23, 92($a0)
        lwc1    $f24, 96($a0)
        lwc1    $f25,100($a0)
        lwc1    $f26,104($a0)
        lwc1    $f27,108($a0)
        lwc1    $f28,112($a0)
        lwc1    $f29,116($a0)
        lwc1    $f30,120($a0)
        lwc1    $f31,124($a0)
        jr      $ra
        nop
        .end    n64_fpu_restore

        .globl  n64_fpu_clear
        .ent    n64_fpu_clear
n64_fpu_clear:
        ctc1    $zero, $31
        mtc1    $zero, $f0
        mtc1    $zero, $f1
        mtc1    $zero, $f2
        mtc1    $zero, $f3
        mtc1    $zero, $f4
        mtc1    $zero, $f5
        mtc1    $zero, $f6
        mtc1    $zero, $f7
        mtc1    $zero, $f8
        mtc1    $zero, $f9
        mtc1    $zero, $f10
        mtc1    $zero, $f11
        mtc1    $zero, $f12
        mtc1    $zero, $f13
        mtc1    $zero, $f14
        mtc1    $zero, $f15
        mtc1    $zero, $f16
        mtc1    $zero, $f17
        mtc1    $zero, $f18
        mtc1    $zero, $f19
        mtc1    $zero, $f20
        mtc1    $zero, $f21
        mtc1    $zero, $f22
        mtc1    $zero, $f23
        mtc1    $zero, $f24
        mtc1    $zero, $f25
        mtc1    $zero, $f26
        mtc1    $zero, $f27
        mtc1    $zero, $f28
        mtc1    $zero, $f29
        mtc1    $zero, $f30
        mtc1    $zero, $f31
        jr      $ra
        nop
        .end    n64_fpu_clear
