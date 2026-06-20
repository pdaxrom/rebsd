#!/bin/sh
cd /var/tmp || exit 1
rm -f cc-smoke cc-smoke.s cc-smoke.o cc-smoke.ro \
    cc-direct \
    cc-fpu-smoke cc-fpu-smoke.s cc-fpu-smoke.o \
    pcc-sysroot-smoke pcc-sysroot-smoke.s pcc-sysroot-smoke.o \
    pcc-sysroot-smoke.ro pcc-sysroot-fpu-smoke \
    pcc-sysroot-fpu-smoke.s pcc-sysroot-fpu-smoke.o

echo "step 1: cc -S"
cc -S -o cc-smoke.s /root/pcc-smoke.c || exit 1
echo "step 2: as"
as -o cc-smoke.o cc-smoke.s || exit 1
echo "step 3: ld -r"
ld -r -o cc-smoke.ro cc-smoke.o || exit 1
test -f cc-smoke.ro || exit 1
echo "step 4: direct ld with /lib/libc.a"
ld -X -d -e _start -o cc-direct /lib/crt0.o cc-smoke.o /lib/libc.a || exit 1
test -f cc-direct || exit 1
./cc-direct || exit 1
echo "direct ld link/run smoke ok"
echo "step 5: cc link"
cc -v -o cc-smoke /root/pcc-smoke.c || exit 1
test -f cc-smoke || exit 1
./cc-smoke || exit 1
echo "cc default sysroot smoke ok"

echo "step 6: cc fpu compile"
cc -S -o cc-fpu-smoke.s /root/pcc-fpu-smoke.c || exit 1
echo "step 7: cc fpu link"
cc -v -o cc-fpu-smoke /root/pcc-fpu-smoke.c || exit 1
test -f cc-fpu-smoke || exit 1
./cc-fpu-smoke || exit 1
echo "cc default sysroot fpu smoke ok"

echo "step 8: pcc -S"
pcc -S -o pcc-sysroot-smoke.s /root/pcc-smoke.c || exit 1
echo "step 9: pcc as"
as -o pcc-sysroot-smoke.o pcc-sysroot-smoke.s || exit 1
echo "step 10: pcc ld -r"
ld -r -o pcc-sysroot-smoke.ro pcc-sysroot-smoke.o || exit 1
test -f pcc-sysroot-smoke.ro || exit 1
echo "step 11: pcc link"
pcc -v -o pcc-sysroot-smoke /root/pcc-smoke.c || exit 1
test -f pcc-sysroot-smoke || exit 1
./pcc-sysroot-smoke || exit 1
echo "pcc default sysroot smoke ok"

echo "step 12: pcc --sysroot fpu compile"
pcc --sysroot / -S -o pcc-sysroot-fpu-smoke.s /root/pcc-fpu-smoke.c || exit 1
echo "step 13: pcc --sysroot fpu link"
pcc --sysroot / -v -o pcc-sysroot-fpu-smoke /root/pcc-fpu-smoke.c || exit 1
test -f pcc-sysroot-fpu-smoke || exit 1
./pcc-sysroot-fpu-smoke || exit 1
echo "pcc --sysroot fpu smoke ok"

rm -f cc-smoke cc-smoke.s cc-smoke.o cc-smoke.ro \
    cc-direct \
    cc-fpu-smoke cc-fpu-smoke.s cc-fpu-smoke.o \
    pcc-sysroot-smoke pcc-sysroot-smoke.s pcc-sysroot-smoke.o \
    pcc-sysroot-smoke.ro pcc-sysroot-fpu-smoke \
    pcc-sysroot-fpu-smoke.s pcc-sysroot-fpu-smoke.o
