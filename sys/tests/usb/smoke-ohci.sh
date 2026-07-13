#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-ohci-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/dev/usb/usb_subr.c" \
    "$top/sys/dev/usb/usb_core.c" \
    "$top/sys/dev/usb/usb_service.c" \
    "$top/sys/dev/usb/ohci.c" \
    "$top/sys/dev/usb/ukbd.c" \
    "$top/sys/dev/usb/ukbdmap.c" \
    ohci_test.c -o "$tmp/ohci_test"

if [ "$mode" = test ]; then
    "$tmp/ohci_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
