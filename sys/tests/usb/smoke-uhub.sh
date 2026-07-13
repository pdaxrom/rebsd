#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-uhub-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/usb/usb_task.c" \
    "$top/sys/usb/usb_subr.c" \
    "$top/sys/usb/usb_core.c" \
    "$top/sys/usb/usb_mock_hcd.c" \
    "$top/sys/usb/uhub.c" \
    uhub_test.c -o "$tmp/uhub_test"

if [ "$mode" = test ]; then
    "$tmp/uhub_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
