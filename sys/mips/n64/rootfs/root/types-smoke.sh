#!/bin/sh
cd /var/tmp || exit 1
rm -f types-cc types-cc.s types-cc.o types-cc.ro \
    types-pcc types-pcc.s types-pcc.o types-pcc.ro

echo "types-smoke diag v1"

echo "step 1: cc types -S"
cc -S -o types-cc.s /root/types-smoke.c || exit 1
echo "step 2: cc types as"
as -o types-cc.o types-cc.s || exit 1
echo "step 3: cc types ld -r"
ld -r -o types-cc.ro types-cc.o || exit 1
test -f types-cc.ro || exit 1
echo "step 4: cc types link/run"
cc -o types-cc /root/types-smoke.c || exit 1
test -f types-cc || exit 1
./types-cc
if test $? != 0; then
    echo "cc generated float/type snippets:"
    sed -n '/^check_float_types:/,/^check_aggregate_types:/p' types-cc.s
    sed -n '/^check_sizes:/,/^check_integer_types:/p' types-cc.s
    exit 1
fi
echo "cc types smoke ok"
rm -f types-cc types-cc.s types-cc.o types-cc.ro

echo "step 5: pcc types -S"
pcc -S -o types-pcc.s /root/types-smoke.c || exit 1
echo "step 6: pcc types as"
as -o types-pcc.o types-pcc.s || exit 1
echo "step 7: pcc types ld -r"
ld -r -o types-pcc.ro types-pcc.o || exit 1
test -f types-pcc.ro || exit 1
echo "step 8: pcc types link/run"
pcc -o types-pcc /root/types-smoke.c || exit 1
test -f types-pcc || exit 1
./types-pcc
if test $? != 0; then
    echo "pcc generated float/type snippets:"
    sed -n '/^check_float_types:/,/^check_aggregate_types:/p' types-pcc.s
    sed -n '/^check_sizes:/,/^check_integer_types:/p' types-pcc.s
    exit 1
fi
echo "pcc types smoke ok"

rm -f types-cc types-cc.s types-cc.o types-cc.ro \
    types-pcc types-pcc.s types-pcc.o types-pcc.ro
