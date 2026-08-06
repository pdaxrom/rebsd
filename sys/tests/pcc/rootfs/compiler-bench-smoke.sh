#!/bin/sh

if test -z "$COMPILER_BENCH_MIN_SECONDS"; then
	COMPILER_BENCH_MIN_SECONDS=0.01
fi
export COMPILER_BENCH_MIN_SECONDS

if test -x /root/compiler-bench-gcc; then
	echo "compiler bench: gcc"
	/root/compiler-bench-gcc || exit 1
fi

echo "compiler bench: pcc"
if test ! -x /root/compiler-bench-pcc; then
	echo "compiler bench: missing /root/compiler-bench-pcc"
	exit 1
fi
/root/compiler-bench-pcc || exit 1

echo "compiler bench ok"
