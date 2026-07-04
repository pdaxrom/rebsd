#!/bin/sh
cd /var/tmp || exit 1
rm -f libc-abi-cc libc-abi-cc.s libc-abi-cc.o libc-abi-cc.ro \
    libc-abi-pcc libc-abi-pcc.s libc-abi-pcc.o libc-abi-pcc.ro

echo "libc-abi-smoke diag v1"

echo "step 1: cc libc/abi -S"
cc -S -o libc-abi-cc.s /root/libc-abi-smoke.c || exit 1
echo "step 2: cc libc/abi as"
as -o libc-abi-cc.o libc-abi-cc.s || exit 1
echo "step 3: cc libc/abi ld -r"
ld -r -o libc-abi-cc.ro libc-abi-cc.o || exit 1
test -f libc-abi-cc.ro || exit 1
echo "step 4: cc libc/abi link/run"
cc -o libc-abi-cc /root/libc-abi-smoke.c || exit 1
test -f libc-abi-cc || exit 1
./libc-abi-cc || exit 1
rm -f libc-abi-cc libc-abi-cc.s libc-abi-cc.o libc-abi-cc.ro

echo "step 5: pcc libc/abi -S"
pcc -S -o libc-abi-pcc.s /root/libc-abi-smoke.c || exit 1
echo "step 6: pcc libc/abi as"
as -o libc-abi-pcc.o libc-abi-pcc.s || exit 1
echo "step 7: pcc libc/abi ld -r"
ld -r -o libc-abi-pcc.ro libc-abi-pcc.o || exit 1
test -f libc-abi-pcc.ro || exit 1
echo "step 8: pcc libc/abi link/run"
pcc -o libc-abi-pcc /root/libc-abi-smoke.c || exit 1
test -f libc-abi-pcc || exit 1
./libc-abi-pcc || exit 1

echo "libc abi smoke ok"
rm -f libc-abi-cc libc-abi-cc.s libc-abi-cc.o libc-abi-cc.ro \
    libc-abi-pcc libc-abi-pcc.s libc-abi-pcc.o libc-abi-pcc.ro
