#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-ci20-usb-hw-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/sys" \
    "$top/sys/mips/ci20/usb_hw.c" \
    ci20_usb_hw_test.c -o "$tmp/ci20_usb_hw_test"

if [ "$mode" = test ]; then
    "$tmp/ci20_usb_hw_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
