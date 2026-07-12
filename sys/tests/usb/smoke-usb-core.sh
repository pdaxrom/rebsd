#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-usb-core-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/sys" \
    "$top/sys/dev/usb/usb_subr.c" \
    "$top/sys/dev/usb/usb_core.c" \
    "$top/sys/dev/usb/usb_mock_hcd.c" \
    usb_core_test.c -o "$tmp/usb_core_test"

if [ "$mode" = test ]; then
    "$tmp/usb_core_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
