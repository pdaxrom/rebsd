#!/bin/sh

device=${1-/dev/sd0}
cd /var || exit 1
rm -f off64-pcc off64-smoke.tmp

echo "off64-smoke: prebuilt GCC against $device"
/usr/bin/off64-smoke-gcc "$device" || exit 1

echo "off64-smoke: native PCC against $device"
pcc -o off64-pcc /root/off64-smoke.c || exit 1
./off64-pcc "$device" || exit 1

rm -f off64-pcc off64-smoke.tmp
echo "off64 smoke both compilers ok"
