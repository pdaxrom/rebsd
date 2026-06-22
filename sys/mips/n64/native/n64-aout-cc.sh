#!/bin/sh
set -e

: ${N64_AOUT_TOPSRC:?}
: ${N64_AOUT_AS:?}

N64_PREFIX=${N64_PREFIX:-/Users/sash/Library/n64-toolchain-opengl/bin/mips64-elf-}
out=a.out
src=
mode=c
args=

while [ $# -gt 0 ]; do
    case "$1" in
    -o)
        shift
        out=$1
        ;;
    -c)
        ;;
    *.c)
        src=$1
        mode=c
        ;;
    *.S|*.s)
        src=$1
        mode=asm
        ;;
    *)
        args="$args '$1'"
        ;;
    esac
    shift
done

if [ -z "$src" ]; then
    echo "n64-aout-cc: no input file" >&2
    exit 1
fi

tmp=${TMPDIR:-/tmp}/n64cc.$$.s
trap 'rm -f "$tmp"' 0 1 2 3 15

if [ "$mode" = c ]; then
    eval "${N64_PREFIX}gcc" \
        -EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32 -mhard-float \
        -G0 -mno-abicalls -fno-pic -fomit-frame-pointer \
        -nostdinc \
        -Wno-unused-value -Wno-format-overflow -Wno-attribute-alias \
        -Wno-missing-attributes \
        -I"'$N64_AOUT_TOPSRC'"/sys/mips/n64/include \
        -I"'$N64_AOUT_TOPSRC'"/include \
        $args -S -o "'$tmp'" "'$src'"
else
    eval "${N64_PREFIX}gcc" \
        -EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32 \
        -G0 -mno-abicalls -fno-pic \
        -nostdinc \
        -I. \
        -I"'$N64_AOUT_TOPSRC'"/sys/mips/n64/include \
        -I"'$N64_AOUT_TOPSRC'"/include \
        $args -x assembler-with-cpp -E -P "'$src'" -o "'$tmp'"
fi

exec "$N64_AOUT_AS" -EB -o "$out" "$tmp"
