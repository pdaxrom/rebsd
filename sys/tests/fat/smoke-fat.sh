#!/bin/sh
set -eu

mode=${1:-test}
cc=${CC:-cc}
root=$(cd ../../.. && pwd)
binary=${TMPDIR:-/tmp}/rebsd-fat-test.$$

trap 'rm -f "$binary"' EXIT HUP INT TERM

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$root/sys" -idirafter "$root/include" \
    "$root/sys/fs/fat/fat_subr.c" fat_test.c -o "$binary"

if [ "$mode" = test ]; then
    "$binary"
elif [ "$mode" = probe ]; then
    test "$#" -eq 3
    "$binary" "$2" "$3"
fi
