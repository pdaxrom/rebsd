#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-usb-desc-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/sys" \
    "$top/sys/dev/usb/usb_subr.c" usb_desc_test.c \
    -o "$tmp/usb_desc_test"

if [ "$mode" = test ]; then
    "$tmp/usb_desc_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
