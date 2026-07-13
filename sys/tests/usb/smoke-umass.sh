#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-umass-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/usb/umass_bbb.c" \
    "$top/sys/usb/umass.c" \
    umass_test.c -o "$tmp/umass_test"

if [ "$mode" = test ]; then
    "$tmp/umass_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
