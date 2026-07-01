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
utf8=$base.utf8.s
utf8_o=$base.utf8.o
half=$base.half.s
half_o=$base.half.o
bad=$base.bad.s
bad_o=$base.bad.o

rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o

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
echo '	jr $ra' >> $valid
echo '	nop' >> $valid

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
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
	;;
esac

echo ".text" > $bad
echo "start:" >> $bad
echo '	movn $2,$3,$4' >> $bad
echo "smoke-as-vr4300: expect reject: movn"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: movn" >&2
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	movz $2,$3,$4' >> $bad
echo "smoke-as-vr4300: expect reject: movz"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: movz" >&2
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	clz $2,$3' >> $bad
echo "smoke-as-vr4300: expect reject: clz"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: clz" >&2
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	ext $2,$3,0,8' >> $bad
echo "smoke-as-vr4300: expect reject: ext"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid instruction: ext" >&2
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
fi

echo ".text" > $bad
echo "start:" >> $bad
echo '	cache 32,0($a0)' >> $bad
echo "smoke-as-vr4300: expect reject: cache 32"
if $as_bin -EB -mips3 -march=vr4300 -o $bad_o $bad; then
	echo "smoke-as-vr4300: accepted invalid cache op" >&2
	rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
	exit 1
fi

rm -f $valid $valid_o $expr $expr_o $utf8 $utf8_o $half $half_o $bad $bad_o
echo "n64 as vr4300 smoke ok"
