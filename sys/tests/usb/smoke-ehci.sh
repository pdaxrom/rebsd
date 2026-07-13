#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-ehci-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/dev/usb/usb_subr.c" \
    "$top/sys/dev/usb/usb_core.c" \
    "$top/sys/dev/usb/usb_task.c" \
    "$top/sys/dev/usb/uhub.c" \
    "$top/sys/dev/usb/ehci.c" \
    ehci_test.c -o "$tmp/ehci_test"

if [ "$mode" = test ]; then
    "$tmp/ehci_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
