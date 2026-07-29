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
    "$top/sys/usb/usb_subr.c" \
    "$top/sys/usb/usb_core.c" \
    "$top/sys/usb/usb_task.c" \
    "$top/sys/usb/usb_service.c" \
    "$top/sys/usb/uhub.c" \
    "$top/sys/usb/ohci.c" \
    "$top/sys/usb/ukbd.c" \
    "$top/sys/usb/ukbd_decode.c" \
    "$top/sys/input/kbdmap.c" \
    ohci_test.c -o "$tmp/ohci_test"

if [ "$mode" = test ]; then
    "$tmp/ohci_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
