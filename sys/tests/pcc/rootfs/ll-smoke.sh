#!/bin/sh
cd /var/tmp || exit 1
rm -f ll-cc ll-cc.s ll-cc.o ll-cc.ro \
    ll-pcc ll-pcc.s ll-pcc.o ll-pcc.ro

echo "ll-smoke diag v3"

echo "step 1: cc long long -S"
cc -S -o ll-cc.s /root/ll-smoke.c || exit 1
echo "step 2: cc long long as"
as -o ll-cc.o ll-cc.s || exit 1
echo "step 3: cc long long ld -r"
ld -r -o ll-cc.ro ll-cc.o || exit 1
test -f ll-cc.ro || exit 1
echo "step 4: cc long long link/run"
cc -o ll-cc /root/ll-smoke.c || exit 1
test -f ll-cc || exit 1
./ll-cc
if test $? != 0; then
    echo "cc generated asm data/main:"
    sed -n '/^gs:/,/^gr:/p' ll-cc.s
    sed -n '/^main:/,/L50:/p' ll-cc.s
    exit 1
fi
echo "cc long long smoke ok"

echo "step 5: pcc long long -S"
pcc -S -o ll-pcc.s /root/ll-smoke.c || exit 1
echo "step 6: pcc long long as"
as -o ll-pcc.o ll-pcc.s || exit 1
echo "step 7: pcc long long ld -r"
ld -r -o ll-pcc.ro ll-pcc.o || exit 1
test -f ll-pcc.ro || exit 1
echo "step 8: pcc long long link/run"
pcc -o ll-pcc /root/ll-smoke.c || exit 1
test -f ll-pcc || exit 1
./ll-pcc
if test $? != 0; then
    echo "pcc generated asm data/main:"
    sed -n '/^gs:/,/^gr:/p' ll-pcc.s
    sed -n '/^main:/,/L50:/p' ll-pcc.s
    exit 1
fi
echo "pcc long long smoke ok"

rm -f ll-cc ll-cc.s ll-cc.o ll-cc.ro \
    ll-pcc ll-pcc.s ll-pcc.o ll-pcc.ro
