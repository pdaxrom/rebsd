#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-pci-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/pci/pci.c" \
    "$top/sys/pci/rtl8169.c" \
    pci_test.c -o "$tmp/pci_test"

if [ "$mode" = test ]; then
    "$tmp/pci_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
