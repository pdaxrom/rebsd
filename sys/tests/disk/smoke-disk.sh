#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-disk-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/disk/disk_subr.c" \
    disk_test.c -o "$tmp/disk_test"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic -DKERNEL -D__mips__ \
    -DDISK_HOST_TEST -I "$top/include" -I "$top/sys" \
    "$top/sys/disk/disk_subr.c" "$top/sys/disk/disk.c" \
    disk_lifecycle_test.c -o "$tmp/disk_lifecycle_test"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -I "$top/src/cmd/fdisk" \
    "$top/src/cmd/fdisk/fdisk_mbr.c" \
    fdisk_mbr_test.c -o "$tmp/fdisk_mbr_test"

if [ "$mode" = test ]; then
    "$tmp/disk_test"
    "$tmp/disk_lifecycle_test"
    "$tmp/fdisk_mbr_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
