#!/bin/sh
#
# One-shot PCC/userland smoke runner for hardware logs.  It avoids network
# tests and keeps all temporary output under /var/tmp.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

if test -z "$CCOM_STRESS_COUNT"; then
	CCOM_STRESS_COUNT=100
fi
if test -z "$LINPACK_ARRAY_SIZE"; then
	LINPACK_ARRAY_SIZE=120
fi
if test -z "$LINPACK_MIN_SECONDS"; then
	LINPACK_MIN_SECONDS=1
fi
export LINPACK_ARRAY_SIZE
export LINPACK_MIN_SECONDS

failures=0

run_smoke()
{
	name=$1
	shift

	echo "PCC_SMOKE_BEGIN $name"
	"$@"
	rc=$?
	echo "PCC_SMOKE_RC $name $rc"
	echo "PCC_SMOKE_END $name"
	if test $rc != 0; then
		failures=`expr $failures + 1`
	fi
}

echo "PCC_SMOKE_ALL_BEGIN"
date
mount
df
ls -ld /var /var/tmp

run_smoke native-pcc /root/native-pcc-smoke.sh
run_smoke cc-pcc /root/cc-pcc-smoke.sh
run_smoke types /root/types-smoke.sh
run_smoke long-long /root/ll-smoke.sh
run_smoke long-long-abi /root/ll-abi-smoke.sh
run_smoke pcc /root/pcc-smoke.sh
run_smoke math-class /root/math-class-smoke.sh
run_smoke libc-string /root/libc-string-smoke.sh
run_smoke libc-abi /root/libc-abi-smoke.sh
run_smoke sh-fail /root/sh-fail-smoke.sh
run_smoke sh-comsubst /root/sh-comsubst-smoke.sh
run_smoke ccom-stress /root/ccom-stress.sh "$CCOM_STRESS_COUNT"
run_smoke linpack /root/linpack-smoke.sh
run_smoke runtime-quick /root/runtime-stress.sh quick

echo "PCC_SMOKE_ALL_FAILURES $failures"
date
if test $failures = 0; then
	echo "PCC_SMOKE_ALL_OK"
	exit 0
fi

echo "PCC_SMOKE_ALL_FAIL"
exit 1
