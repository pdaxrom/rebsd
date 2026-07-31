#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-uhci-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/tests/usb/hw_inventory_stub.c" \
    "$top/sys/usb/usb_subr.c" \
    "$top/sys/usb/usb_core.c" \
    "$top/sys/usb/usb_service.c" \
    "$top/sys/usb/usb_task.c" \
    "$top/sys/usb/uhci.c" \
    "$top/sys/usb/ukbd.c" \
    "$top/sys/usb/ukbd_decode.c" \
    "$top/sys/input/kbdmap.c" \
    uhci_test.c -o "$tmp/uhci_test"

if [ "$mode" = test ]; then
    "$tmp/uhci_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
