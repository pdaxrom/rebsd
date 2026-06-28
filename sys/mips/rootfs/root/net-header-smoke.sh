#!/bin/sh
cd /var/tmp || exit 1
rm -f net-header-cc net-header-cc.s net-header-cc.o net-header-cc.ro \
    net-header-pcc net-header-pcc.s net-header-pcc.o net-header-pcc.ro

echo "net-header-smoke diag v1"

echo "step 1: cc headers -S"
cc -S -o net-header-cc.s /root/net-header-smoke.c || exit 1
echo "step 2: cc headers as"
as -o net-header-cc.o net-header-cc.s || exit 1
echo "step 3: cc headers ld -r"
ld -r -o net-header-cc.ro net-header-cc.o || exit 1
test -f net-header-cc.ro || exit 1
echo "step 4: cc headers link/run"
cc -o net-header-cc /root/net-header-smoke.c || exit 1
./net-header-cc || exit 1
echo "cc header smoke ok"
rm -f net-header-cc net-header-cc.s net-header-cc.o net-header-cc.ro

echo "step 5: pcc headers -S"
pcc -S -o net-header-pcc.s /root/net-header-smoke.c || exit 1
echo "step 6: pcc headers as"
as -o net-header-pcc.o net-header-pcc.s || exit 1
echo "step 7: pcc headers ld -r"
ld -r -o net-header-pcc.ro net-header-pcc.o || exit 1
test -f net-header-pcc.ro || exit 1
echo "step 8: pcc headers link/run"
pcc -o net-header-pcc /root/net-header-smoke.c || exit 1
./net-header-pcc || exit 1
echo "pcc header smoke ok"

rm -f net-header-cc net-header-cc.s net-header-cc.o net-header-cc.ro \
    net-header-pcc net-header-pcc.s net-header-pcc.o net-header-pcc.ro
