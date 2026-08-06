#!/bin/sh
cd /var/tmp || exit 1
rm -f llabi-asm.o \
    llabi-cc llabi-cc.s llabi-cc.o llabi-cc.ro \
    llabi-pcc llabi-pcc.s llabi-pcc.o llabi-pcc.ro

echo "ll-abi-smoke diag v1"

echo "step 1: as ABI checker"
cc -x assembler-with-cpp -c -o llabi-asm.o /root/ll-abi-smoke.s || exit 1

echo "step 2: cc ABI -S"
cc -S -o llabi-cc.s /root/ll-abi-smoke.c || exit 1
echo "step 3: cc ABI as"
as -o llabi-cc.o llabi-cc.s || exit 1
echo "step 4: cc ABI ld -r"
ld -r -o llabi-cc.ro llabi-cc.o llabi-asm.o || exit 1
echo "step 5: cc ABI link/run"
ld -X -d -e _start -o llabi-cc /usr/lib/crt0.o llabi-cc.o llabi-asm.o \
    /usr/lib/libc.a || exit 1
./llabi-cc || exit 1
echo "cc long long ABI smoke ok"

echo "step 6: pcc ABI -S"
pcc -S -o llabi-pcc.s /root/ll-abi-smoke.c || exit 1
echo "step 7: pcc ABI as"
as -o llabi-pcc.o llabi-pcc.s || exit 1
echo "step 8: pcc ABI ld -r"
ld -r -o llabi-pcc.ro llabi-pcc.o llabi-asm.o || exit 1
echo "step 9: pcc ABI link/run"
ld -X -d -e _start -o llabi-pcc /usr/lib/crt0.o llabi-pcc.o llabi-asm.o \
    /usr/lib/libc.a || exit 1
./llabi-pcc || exit 1
echo "pcc long long ABI smoke ok"

rm -f llabi-asm.o \
    llabi-cc llabi-cc.s llabi-cc.o llabi-cc.ro \
    llabi-pcc llabi-pcc.s llabi-pcc.o llabi-pcc.ro
