#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-rtc-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic -DKERNEL \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/clock_subr.c" \
    "$top/sys/i2c/i2c.c" \
    "$top/sys/rtc/mc146818.c" \
    "$top/sys/rtc/pcf8563.c" \
    "$top/sys/rtc/jz4780.c" \
    rtc_test.c -o "$tmp/rtc_test"

if [ "$mode" = test ]; then
    "$tmp/rtc_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
