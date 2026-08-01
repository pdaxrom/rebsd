#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-rtc-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
cp "$top/include/tzfile.h" "$tmp/tzfile.h"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic -DKERNEL \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/clock_subr.c" \
    "$top/sys/i2c/i2c.c" \
    "$top/sys/rtc/mc146818.c" \
    "$top/sys/rtc/pcf8563.c" \
    "$top/sys/rtc/jz4780.c" \
    rtc_test.c -o "$tmp/rtc_test"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$tmp" \
    "$top/src/share/zoneinfo/zic.c" \
    "$top/src/share/zoneinfo/scheck.c" \
    "$top/src/share/zoneinfo/ialloc.c" -o "$tmp/zic"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$tmp" \
    -D_PATH_LOCALTIME='"/etc/localtime"' \
    -D_PATH_ZONEINFO='"/usr/share/zoneinfo"' \
    "$top/src/libc/gen/ctime.c" "$top/src/libc/gen/mktime.c" \
    "$top/src/libc/gen/ctime_r.c" time64_tzif_test.c \
    -o "$tmp/time64_tzif_test"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
    -I"$top/src/cmd/ntpdate" \
    "$top/src/cmd/ntpdate/ntp_proto.c" time64_ntp_test.c \
    -o "$tmp/time64_ntp_test"

if [ "$mode" = test ]; then
    "$tmp/rtc_test"
    mkdir -p "$tmp/zoneinfo"
    "$tmp/zic" -d "$tmp/zoneinfo" \
        "$top/src/share/zoneinfo/northamerica"
    "$tmp/time64_tzif_test" "$tmp/zoneinfo/US/Pacific"
    "$tmp/time64_ntp_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
