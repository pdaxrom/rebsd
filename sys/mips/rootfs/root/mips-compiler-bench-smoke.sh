#!/bin/sh

if test -z "$MIPS_COMPILER_BENCH_MIN_SECONDS"; then
	MIPS_COMPILER_BENCH_MIN_SECONDS=0.01
fi
export MIPS_COMPILER_BENCH_MIN_SECONDS

if test -x /root/mips-compiler-bench-gcc; then
	echo "mips compiler bench: gcc"
	/root/mips-compiler-bench-gcc || exit 1
fi

echo "mips compiler bench: pcc"
if test ! -x /root/mips-compiler-bench-pcc; then
	echo "mips compiler bench: missing /root/mips-compiler-bench-pcc"
	exit 1
fi
/root/mips-compiler-bench-pcc || exit 1

echo "mips compiler bench ok"
