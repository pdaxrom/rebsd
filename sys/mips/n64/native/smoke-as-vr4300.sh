#!/bin/sh
#
# Simple smoke test for the N64 in-tree assembler build.
#

if test $# -gt 1; then
	echo "usage: $0 [/path/to/as]" >&2
	exit 2
fi

if test $# -eq 1; then
	as_bin=$1
else
	as_bin=/usr/bin/as
fi

cd /tmp || cd /var/tmp || exit 1

base=n64-as-vr4300.$$
valid=$base.valid.s
valid_o=$base.valid.o
expr=$base.expr.s
expr_o=$base.expr.o
loaddend=$base.loaddend.s
loaddend_o=$base.loaddend.o
hazard=$base.hazard.s
hazard_o=$base.hazard.o
callslot=$base.callslot.s
callslot_o=$base.callslot.o
controlslot=$base.controlslot.s
controlslot_o=$base.controlslot.o
optslot=$base.optslot.s
optslot_o=$base.optslot.o
likelyslot=$base.likelyslot.s
likelyslot_o=$base.likelyslot.o
depslot=$base.depslot.s
depslot_o=$base.depslot.o
backslot=$base.backslot.s
backslot_o=$base.backslot.o
utf8=$base.utf8.s
utf8_o=$base.utf8.o
half=$base.half.s
half_o=$base.half.o
bad=$base.bad.s
bad_o=$base.bad.o
mips32=$base.mips32.s
mips32_o=$base.mips32.o

rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
    $hazard $hazard_o $callslot $callslot_o \
    $controlslot $controlslot_o \
    $optslot $optslot_o $likelyslot $likelyslot_o \
    $depslot $depslot_o $backslot $backslot_o \
    $utf8 $utf8_o $half $half_o \
    $bad $bad_o $mips32 $mips32_o

$as_bin --target-info || exit 1

echo ".text" > $valid
echo "start:" >> $valid
echo '	cache 0x10,0($a0)' >> $valid
echo '	cache 16,32($sp)' >> $valid
echo '	tlbp' >> $valid
echo '	tlbr' >> $valid
echo '	tlbwi' >> $valid
echo '	tlbwr' >> $valid
echo '	wait' >> $valid
echo '	eret' >> $valid
echo '	mtc1 $0,$f0' >> $valid
echo '	mtc1 $0,$f1' >> $valid
echo '	c.eq.d $f0,$f0' >> $valid
echo '	bc1t 1f' >> $valid
echo '	nop' >> $valid
echo '1:' >> $valid
echo '	add.d $f2,$f0,$f0' >> $valid
echo '	round.w.d $f4,$f2' >> $valid
echo '	cfc1 $2,$31' >> $valid
echo '	ctc1 $2,$31' >> $valid
echo '	mul $2,$3,$4' >> $valid
echo '	neg $2,$2' >> $valid
echo '	negu $3,$3' >> $valid
echo '	lw $2,46928($4)' >> $valid
echo '	sw $3,44876($4)' >> $valid
echo '	ld $6,16($sp)' >> $valid
echo '	sd $6,24($sp)' >> $valid
echo '	ld $at,wide_data' >> $valid
echo '	sd $at,32($sp)' >> $valid
echo '	lh $5,-40000($4)' >> $valid
echo '	sh $5,-40000($4)' >> $valid
echo '	jr $ra' >> $valid
echo '	nop' >> $valid
echo ".data" >> $valid
echo "wide_data:" >> $valid
echo '	.word 0' >> $valid
echo '	.word 0' >> $valid

$as_bin -EB -mips3 -march=vr4300 -o $valid_o $valid || exit 1

echo ".data" > $expr
echo "sym:" >> $expr
echo '	.word 0' >> $expr
echo ".text" >> $expr
echo "expr_start:" >> $expr
echo '	la $2,sym+-8' >> $expr
echo '	jr $ra' >> $expr
echo '	nop' >> $expr
$as_bin -EB -mips3 -march=vr4300 -o $expr_o $expr || exit 1

# GCC emits this form for addresses beyond the first signed 16-bit window of
# a section.  %lo selects the low half; the full addend must not be rejected as
# an out-of-range addiu immediate.
echo ".bss" > $loaddend
echo "pool:" >> $loaddend
echo '    .space 4' >> $loaddend
echo ".text" >> $loaddend
echo "loaddend_start:" >> $loaddend
echo '    lui $3,%hi(pool+33408)' >> $loaddend
echo '    addiu $3,$3,%lo(pool+33408)' >> $loaddend
echo '    jr $ra' >> $loaddend
echo '    nop' >> $loaddend
$as_bin --elf -EB -mips3 -march=vr4300 -o $loaddend_o $loaddend || exit 1

echo ".text" > $hazard
echo ".set noreorder" >> $hazard
echo "hazard_start:" >> $hazard
echo '    mflo $2' >> $hazard
echo '    mult $3,$4' >> $hazard
$as_bin -EB -mips3 -march=vr4300 -o $hazard_o $hazard || exit 1
hazard_bytes=`od -An -tx1 -j 32 -N 16 $hazard_o | tr -d ' \n'`
if test "$hazard_bytes" != "00001012000000000000000000640018"; then
	echo "smoke-as-vr4300: bad mflo/mult hazard padding: $hazard_bytes" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

# In reorder mode a call must not pull a preceding save of $ra into its delay
# slot.  jal installs its own return address before that slot executes, so the
# moved store would destroy the caller's return address.
echo ".text" > $callslot
echo ".set reorder" >> $callslot
echo "callslot_start:" >> $callslot
echo '    addiu $sp,$sp,-24' >> $callslot
echo '    sw $ra,20($sp)' >> $callslot
echo '    jal callslot_target' >> $callslot
echo '    addiu $2,$zero,7' >> $callslot
echo "callslot_target:" >> $callslot
echo '    jr $ra' >> $callslot
echo '    nop' >> $callslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $callslot_o $callslot || exit 1
callslot_bytes=`od -An -tx1 -j 32 -N 28 $callslot_o | tr -d ' \n'`
if test "$callslot_bytes" != \
    "27bdffe8afbf00140c000005000000002402000703e0000800000000"; then
	echo "smoke-as-vr4300: unsafe jal delay-slot scheduling: $callslot_bytes" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o $callslot $callslot_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

# Reorder mode must preserve source order around every control transfer.
# A single-register dependency check cannot prove a store, SYNC, trapping
# instruction, or branch-likely candidate safe in a delay slot.
echo ".text" > $controlslot
echo ".set reorder" >> $controlslot
echo "controlslot_start:" >> $controlslot
echo '    sw $2,0($3)' >> $controlslot
echo '    b controlslot_target' >> $controlslot
echo '    sync' >> $controlslot
echo "controlslot_target:" >> $controlslot
echo '    jr $ra' >> $controlslot
echo '    nop' >> $controlslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $controlslot_o $controlslot || exit 1
controlslot_bytes=`od -An -tx1 -j 32 -N 24 $controlslot_o | tr -d ' \n'`
if test "$controlslot_bytes" != \
    "ac62000010000002000000000000000f03e0000800000000"; then
	echo "smoke-as-vr4300: unsafe control delay-slot scheduling: $controlslot_bytes" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o $callslot $callslot_o \
	    $controlslot $controlslot_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

# Like BSD/GAS, delay-slot filling is an explicit -O2 optimization.  Only a
# non-trapping GPR operation with no branch dependency is eligible.
echo ".text" > $optslot
echo ".set reorder" >> $optslot
echo "optslot_start:" >> $optslot
echo '    addiu $2,$2,1' >> $optslot
echo '    b optslot_target' >> $optslot
echo '    addiu $3,$3,1' >> $optslot
echo "optslot_target:" >> $optslot
echo ".set noreorder" >> $optslot
echo '    jr $ra' >> $optslot
echo '    nop' >> $optslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $optslot_o $optslot || exit 1
optslot_bytes=`od -An -tx1 -j 32 -N 20 $optslot_o | tr -d ' \n'`
if test "$optslot_bytes" != \
    "10000002244200012463000103e0000800000000"; then
	echo "smoke-as-vr4300: safe -O2 delay slot not filled: $optslot_bytes" >&2
	exit 1
fi

# A likely branch annuls its slot when not taken, so moving an instruction
# there changes whether it executes and must always be rejected.
echo ".text" > $likelyslot
echo ".set reorder" >> $likelyslot
echo "likelyslot_start:" >> $likelyslot
echo '    addiu $2,$2,1' >> $likelyslot
echo '    beql $3,$zero,likelyslot_target' >> $likelyslot
echo "likelyslot_target:" >> $likelyslot
echo ".set noreorder" >> $likelyslot
echo '    jr $ra' >> $likelyslot
echo '    nop' >> $likelyslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $likelyslot_o $likelyslot || exit 1
likelyslot_bytes=`od -An -tx1 -j 32 -N 20 $likelyslot_o | tr -d ' \n'`
if test "$likelyslot_bytes" != \
    "24420001506000010000000003e0000800000000"; then
	echo "smoke-as-vr4300: likely branch was reordered: $likelyslot_bytes" >&2
	exit 1
fi

# Reversing a producer and a branch that consumes its result is not safe.
echo ".text" > $depslot
echo ".set reorder" >> $depslot
echo "depslot_start:" >> $depslot
echo '    addiu $2,$2,1' >> $depslot
echo '    bne $2,$3,depslot_target' >> $depslot
echo "depslot_target:" >> $depslot
echo ".set noreorder" >> $depslot
echo '    jr $ra' >> $depslot
echo '    nop' >> $depslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $depslot_o $depslot || exit 1
depslot_bytes=`od -An -tx1 -j 32 -N 20 $depslot_o | tr -d ' \n'`
if test "$depslot_bytes" != \
    "24420001144300010000000003e0000800000000"; then
	echo "smoke-as-vr4300: dependent branch was reordered: $depslot_bytes" >&2
	exit 1
fi

# A backward local branch is fully resolved before output.  Moving it one
# word earlier must also adjust its already-encoded PC-relative displacement.
echo ".text" > $backslot
echo ".set reorder" >> $backslot
echo "backslot_target:" >> $backslot
echo '    addiu $2,$2,1' >> $backslot
echo '    bne $3,$4,backslot_target' >> $backslot
echo ".set noreorder" >> $backslot
echo '    jr $ra' >> $backslot
echo '    nop' >> $backslot
$as_bin -O2 -EB -mips3 -march=vr4300 -o $backslot_o $backslot || exit 1
backslot_bytes=`od -An -tx1 -j 32 -N 16 $backslot_o | tr -d ' \n'`
if test "$backslot_bytes" != \
    "1464ffff2442000103e0000800000000"; then
	echo "smoke-as-vr4300: bad backward delay-slot displacement: $backslot_bytes" >&2
	exit 1
fi

printf '.data\n\t.globl \303\244\n\303\244:\n\t.word 1\n' > $utf8
$as_bin -EB -mips3 -march=vr4300 -o $utf8_o $utf8 || exit 1

echo ".data" > $half
echo "halfprobe:" >> $half
echo '	.half 0x1234' >> $half
echo '	.half 0xfffe' >> $half
echo '	.half 0x5678' >> $half
echo '	.half 0x9abc' >> $half
$as_bin -EB -mips3 -march=vr4300 -o $half_o $half || exit 1
bytes=`od -b $half_o | tr -d ' \n'`
case "$bytes" in
*022064377376126170232274*) ;;
*)
	echo "smoke-as-vr4300: bad .half big-endian output" >&2
	od -b $half_o
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
	;;
esac

echo ".text" > $mips32
echo "start:" >> $mips32
echo '	ld $2,0($3)' >> $mips32
echo "smoke-as-vr4300: expect reject: mips32r2 ld"
if $as_bin -EB -mips32r2 -march=mips32r2 -o $mips32_o $mips32; then
	echo "smoke-as-vr4300: accepted MIPS3 ld in mips32r2 mode" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $mips32
echo "start:" >> $mips32
echo '	sd $2,0($3)' >> $mips32
echo "smoke-as-vr4300: expect reject: mips32r2 sd"
if $as_bin -EB -mips32r2 -march=mips32r2 -o $mips32_o $mips32; then
	echo "smoke-as-vr4300: accepted MIPS3 sd in mips32r2 mode" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	movn $2,$3,$4' >> $bad
echo "smoke-as-vr4300: expect reject: movn"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: movn" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	movz $2,$3,$4' >> $bad
echo "smoke-as-vr4300: expect reject: movz"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: movz" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	clz $2,$3' >> $bad
echo "smoke-as-vr4300: expect reject: clz"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: clz" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	ext $2,$3,0,8' >> $bad
echo "smoke-as-vr4300: expect reject: ext"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: ext" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	cache 32,0($a0)' >> $bad
echo "smoke-as-vr4300: expect reject: cache 32"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid cache op" >&2
	rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
	    $hazard $hazard_o \
	    $utf8 $utf8_o $half $half_o \
	    $bad $bad_o $mips32 $mips32_o
	exit 1
fi

rm -f $valid $valid_o $expr $expr_o $loaddend $loaddend_o \
    $hazard $hazard_o $callslot $callslot_o \
    $controlslot $controlslot_o \
    $optslot $optslot_o $likelyslot $likelyslot_o \
    $depslot $depslot_o $backslot $backslot_o \
    $utf8 $utf8_o $half $half_o \
    $bad $bad_o $mips32 $mips32_o
echo "n64 as vr4300 smoke ok"
