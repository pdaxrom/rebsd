#!/bin/sh

if test -z "$LINPACK_ARRAY_SIZE"; then
    LINPACK_ARRAY_SIZE=120
fi
if test -z "$LINPACK_MIN_SECONDS"; then
    LINPACK_MIN_SECONDS=1
fi
export LINPACK_ARRAY_SIZE
export LINPACK_MIN_SECONDS

if test -x /root/linpack-gcc; then
    echo "linpack smoke: gcc"
    /root/linpack-gcc || exit 1
fi

echo "linpack smoke: pcc"
if test ! -x /root/linpack-pcc; then
    echo "linpack smoke: missing /root/linpack-pcc"
    exit 1
fi
/root/linpack-pcc || exit 1

echo "linpack smoke ok"
