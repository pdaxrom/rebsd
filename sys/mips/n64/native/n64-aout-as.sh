#!/bin/sh
set -e

: ${N64_AOUT_TOPSRC:?}
: ${N64_AOUT_AS:?}

N64_PREFIX=${N64_PREFIX:-/Users/sash/Library/n64-toolchain-opengl/bin/mips64-elf-}
out=a.out
src=
args=

while [ $# -gt 0 ]; do
    case "$1" in
    -o)
        shift
        out=$1
        ;;
    -c|-x|assembler-with-cpp)
        ;;
    *.S|*.s)
        src=$1
        ;;
    -)
        src=-
        ;;
    *)
        args="$args '$1'"
        ;;
    esac
    shift
done

if [ -z "$src" ]; then
    src=-
fi

tmp=${TMPDIR:-/tmp}/n64as.$$.s
trap 'rm -f "$tmp"' 0 1 2 3 15

eval "${N64_PREFIX}gcc" \
    -EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32 \
    -G0 -mno-abicalls -fno-pic \
    -I. \
    -I"'$N64_AOUT_TOPSRC'"/sys/mips/n64/include \
    -I"'$N64_AOUT_TOPSRC'"/include \
    $args -x assembler-with-cpp -E -P "'$src'" -o "'$tmp'"

exec "$N64_AOUT_AS" -EB -o "$out" "$tmp"
