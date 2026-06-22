#!/bin/sh
cd /var/tmp || exit 1
rm -f a.out pcc-smoke pcc-smoke.s pcc-smoke.o pcc-smoke.ro \
    pcc-fpu-smoke pcc-fpu-smoke.s pcc-fpu-smoke.o pcc-fpu-smoke.ro

pcc -S -o pcc-smoke.s /root/pcc-smoke.c || exit 1
as -o pcc-smoke.o pcc-smoke.s || exit 1
ld -r -o pcc-smoke.ro pcc-smoke.o || exit 1
test -f pcc-smoke.ro || exit 1
echo "pcc/as/ld -r smoke ok"
pcc -o pcc-smoke /root/pcc-smoke.c || exit 1
test -f pcc-smoke || exit 1
./pcc-smoke || exit 1
echo "pcc link/run smoke ok"

pcc -S -o pcc-fpu-smoke.s /root/pcc-fpu-smoke.c || exit 1
echo "pcc fpu compile smoke ok"
pcc -o pcc-fpu-smoke /root/pcc-fpu-smoke.c || exit 1
test -f pcc-fpu-smoke || exit 1
./pcc-fpu-smoke || exit 1
echo "pcc fpu link/run smoke ok"

rm -f a.out pcc-smoke pcc-smoke.s pcc-smoke.o pcc-smoke.ro \
    pcc-fpu-smoke pcc-fpu-smoke.s pcc-fpu-smoke.o pcc-fpu-smoke.ro
