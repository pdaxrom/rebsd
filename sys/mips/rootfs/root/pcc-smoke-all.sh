#!/bin/sh
#
# One-shot PCC/userland smoke runner for hardware logs.  It avoids network
# tests and keeps all temporary output under /var/tmp.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

# Diagnostic images can trace every program spawned by the PCC driver.  An
# N64 minimal image supplies the build-time default through this file; an
# explicit environment setting takes precedence for one-off comparisons.
if test -z "$PCC_SMOKE_TRACE" && test -r /etc/pcc-smoke.conf; then
	. /etc/pcc-smoke.conf
fi
if test -z "$PCC_SMOKE_TRACE"; then
	PCC_SMOKE_TRACE=0
fi
case "$PCC_SMOKE_TRACE" in
1|yes|true)
	if test -z "$PCC_EXEC_TRACE"; then
		PCC_EXEC_TRACE=full
	fi
	if test -z "$PCC_CCOM_TRACE"; then
		PCC_CCOM_TRACE=1
	fi
	export PCC_EXEC_TRACE
	export PCC_CCOM_TRACE
	;;
*)
	unset PCC_EXEC_TRACE
	unset PCC_CCOM_TRACE
	;;
esac

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
failed_tests=

resource_snapshot()
{
	echo "PCC_SMOKE_RESOURCES_BEGIN $1"
	free
	pstat -T
	df
	echo "PCC_SMOKE_RESOURCES_END $1"
}

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
		resource_snapshot "failure-$name"
		failures=`expr $failures + 1`
		failed_tests="$failed_tests $name:$rc"
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
run_smoke libpcc-helper /root/libpcc-helper-smoke.sh
run_smoke pcc /root/pcc-smoke.sh
run_smoke math-class /root/math-class-smoke.sh
run_smoke wide-policy /root/wide-policy-smoke.sh
run_smoke libc-string /root/libc-string-smoke.sh
run_smoke libc-abi /root/libc-abi-smoke.sh
run_smoke alias-stress /root/alias-stress-smoke.sh
run_smoke build-workload /root/build-workload-smoke.sh
run_smoke make-workload /root/make-workload-smoke.sh
run_smoke utility-workload /root/utility-workload-smoke.sh
run_smoke sh-fail /root/sh-fail-smoke.sh
run_smoke sh-comsubst /root/sh-comsubst-smoke.sh
run_smoke ccom-stress /root/ccom-stress.sh "$CCOM_STRESS_COUNT"
run_smoke linpack /root/linpack-smoke.sh
run_smoke compiler-bench /root/mips-compiler-bench-smoke.sh
run_smoke runtime-quick /root/runtime-stress.sh quick

resource_snapshot end
echo "PCC_SMOKE_ALL_FAILURES $failures"
if test $failures != 0; then
	echo "PCC_SMOKE_ALL_FAILED_TESTS$failed_tests"
fi
date
if test $failures = 0; then
	echo "PCC_SMOKE_ALL_OK"
	exit 0
fi

echo "PCC_SMOKE_ALL_FAIL"
exit 1
