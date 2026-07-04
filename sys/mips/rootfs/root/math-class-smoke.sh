#!/bin/sh
cd /var/tmp || exit 1
rm -f math-class-cc math-class-cc.s math-class-pcc math-class-pcc.s

echo "math-class smoke: cc"
cc -S -o math-class-cc.s /root/math-class-smoke.c || exit 1
cc -o math-class-cc /root/math-class-smoke.c || exit 1
./math-class-cc || exit 1

echo "math-class smoke: pcc"
pcc -S -o math-class-pcc.s /root/math-class-smoke.c || exit 1
pcc -o math-class-pcc /root/math-class-smoke.c || exit 1
./math-class-pcc || exit 1

rm -f math-class-cc math-class-cc.s math-class-pcc math-class-pcc.s
echo "math-class smoke ok"
