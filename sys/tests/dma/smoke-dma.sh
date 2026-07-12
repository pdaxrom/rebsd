#!/bin/sh
set -eu

top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-dma-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" \
    "$top/sys/kernel/subr_dma.c" dma_test.c -o "$tmp/dma_test"
"$tmp/dma_test"
