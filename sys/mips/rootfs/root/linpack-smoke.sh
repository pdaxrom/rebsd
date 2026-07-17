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

if test "$LINPACK_KERNEL_BENCH" = 1; then
    if test -x /root/linpack-kernels-gcc; then
        echo "linpack kernel bench: gcc"
        /root/linpack-kernels-gcc || exit 1
    fi

    echo "linpack kernel bench: pcc"
    if test ! -x /root/linpack-kernels-pcc; then
        echo "linpack kernel bench: missing /root/linpack-kernels-pcc"
        exit 1
    fi
    /root/linpack-kernels-pcc || exit 1
fi

echo "linpack smoke ok"
