#!/bin/sh
set -e

top=${1:-$(pwd)}
out=${TMPDIR:-/tmp}/n64usbnet-frame-smoke.$$
trap 'rm -f "$out"' 0 1 2 3 15

cc -Wall -Wextra -Werror \
    -I"$top/sys/mips/common" \
    "$top/sys/mips/common/n64usbnet_proto.c" \
    "$top/sys/mips/n64/native/smoke-usbnet-frame.c" \
    -o "$out"
"$out"
