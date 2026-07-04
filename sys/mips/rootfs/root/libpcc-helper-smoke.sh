#!/bin/sh
cd /var/tmp || exit 1
rm -f libpcc-helper-cc libpcc-helper-cc.s \
    libpcc-helper-pcc libpcc-helper-pcc.s

echo "libpcc-helper smoke: cc"
cc -S -o libpcc-helper-cc.s /root/libpcc-helper-smoke.c || exit 1
cc -o libpcc-helper-cc /root/libpcc-helper-smoke.c || exit 1
./libpcc-helper-cc || exit 1

echo "libpcc-helper smoke: pcc"
pcc -S -o libpcc-helper-pcc.s /root/libpcc-helper-smoke.c || exit 1
pcc -o libpcc-helper-pcc /root/libpcc-helper-smoke.c || exit 1
./libpcc-helper-pcc || exit 1

rm -f libpcc-helper-cc libpcc-helper-cc.s \
    libpcc-helper-pcc libpcc-helper-pcc.s
echo "libpcc-helper smoke ok"
