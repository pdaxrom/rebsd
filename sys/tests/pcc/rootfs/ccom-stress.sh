#!/bin/sh
count=${1-50}

cd /var/tmp || exit 1
rm -f ccom-stress-fpu ccom-stress-fpu.c \
    ccom-stress-nan.c ccom-stress-nan.s

cat > ccom-stress-fpu.c <<'EOF'
int
main(void)
{
    volatile double z = 0.0;
    volatile double x = z / z;

    return x == x;
}
EOF

cat > ccom-stress-nan.c <<'EOF'
double
f(void)
{
    return 0.0 / 0.0;
}

int
main(void)
{
    return 0;
}
EOF

echo CCOM_STRESS_BEGIN:$count
sysctl vm.pmap_rollovers vm.pageins vm.pageouts vm.swap_failures
cc -o ccom-stress-fpu ccom-stress-fpu.c || exit 2
./ccom-stress-fpu
fpu_rc=$?
echo CCOM_STRESS_FPU_RC:$fpu_rc

i=0
next_report=100
while test $i -lt $count; do
    cc -S -o ccom-stress-nan.s ccom-stress-nan.c
    rc=$?
    if test $rc -ne 0; then
        echo CCOM_STRESS_FAIL:$i:$rc
        free
        sysctl vm.pmap_rollovers vm.pageins vm.pageouts vm.swap_failures
        exit 10
    fi
    i=`expr $i + 1`
    if test $i -eq $next_report; then
        echo CCOM_STRESS_PROGRESS:$i
        sysctl vm.pmap_rollovers vm.pageins vm.pageouts vm.swap_failures
        next_report=`expr $next_report + 100`
    fi
done

echo CCOM_STRESS_DONE:$i
sysctl vm.pmap_rollovers vm.pageins vm.pageouts vm.swap_failures
rm -f ccom-stress-fpu ccom-stress-fpu.c \
    ccom-stress-nan.c ccom-stress-nan.s
