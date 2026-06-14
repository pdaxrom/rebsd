#!/bin/sh
cd /var/tmp || exit 1
rm -f a.out pcc-smoke.s pcc-smoke.o pcc-smoke.ro pcc-fpu-smoke.s

if pcc -S -o pcc-smoke.s /root/pcc-smoke.c; then :; else exit 1; fi
if as -o pcc-smoke.o pcc-smoke.s; then :; else exit 1; fi
if ld -r -o pcc-smoke.ro pcc-smoke.o; then :; else exit 1; fi
echo "pcc/as/ld -r smoke ok"

if pcc -S -o pcc-fpu-smoke.s /root/pcc-fpu-smoke.c; then :; else exit 1; fi
echo "pcc fpu compile smoke ok"

rm -f a.out pcc-smoke.s pcc-smoke.o pcc-smoke.ro pcc-fpu-smoke.s
