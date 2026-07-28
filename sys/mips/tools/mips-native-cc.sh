#!/bin/sh
set -e

: ${MIPS_NATIVE_TOPSRC:?}
: ${MIPS_NATIVE_AS:?}
: ${MIPS_NATIVE_PREFIX:?}
: ${MIPS_NATIVE_CPU:?}
: ${MIPS_NATIVE_FLOAT:?}
: ${MIPS_NATIVE_ENDIAN:?}

case "$MIPS_NATIVE_ENDIAN" in
big)
    gcc_endian_flag="-EB"
    as_endian_flag="-EB"
    ;;
little)
    gcc_endian_flag="-EL"
    as_endian_flag="-EL"
    ;;
*)
    echo "mips-native-cc: unsupported MIPS_NATIVE_ENDIAN=$MIPS_NATIVE_ENDIAN" >&2
    exit 1
    ;;
esac
case "$MIPS_NATIVE_CPU" in
vr4300)
    arch_flags="$gcc_endian_flag -march=vr4300 -mtune=vr4300 -mips3 -mabi=32"
    as_cpu_flag="-march=vr4300"
    ;;
mips32r2)
    arch_flags="$gcc_endian_flag -march=mips32r2 -mips32r2 -mtune=24kf -mabi=32"
    as_cpu_flag="-march=mips32r2"
    ;;
*)
    echo "mips-native-cc: unsupported MIPS_NATIVE_CPU=$MIPS_NATIVE_CPU" >&2
    exit 1
    ;;
esac
case "$MIPS_NATIVE_FLOAT" in
hard)
    float_flag="-mhard-float"
    ;;
soft)
    float_flag="-msoft-float"
    ;;
*)
    echo "mips-native-cc: unsupported MIPS_NATIVE_FLOAT=$MIPS_NATIVE_FLOAT" >&2
    exit 1
    ;;
esac
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
    -)
        src=-
        mode=asm
        ;;
    *)
        args="$args '$1'"
        ;;
    esac
    shift
done

if [ -z "$src" ]; then
    echo "mips-native-cc: no input file" >&2
    exit 1
fi

tmp=${TMPDIR:-/tmp}/mipscc.$$.s
trap 'rm -f "$tmp"' 0 1 2 3 15

if [ "$mode" = c ]; then
    eval "${MIPS_NATIVE_PREFIX}gcc" \
        $arch_flags $float_flag \
        -G0 -mno-abicalls -fno-pic -fomit-frame-pointer \
        -nostdinc \
        -Wno-unused-value -Wno-format-overflow -Wno-attribute-alias \
        -Wno-missing-attributes \
        -I"'$MIPS_NATIVE_TOPSRC'"/sys/mips/n64/include \
        -I"'$MIPS_NATIVE_TOPSRC'"/include \
        $args -S -o "'$tmp'" "'$src'"
else
    eval "${MIPS_NATIVE_PREFIX}gcc" \
        $arch_flags $float_flag \
        -G0 -mno-abicalls -fno-pic \
        -nostdinc \
        -I. \
        -I"'$MIPS_NATIVE_TOPSRC'"/sys/mips/n64/include \
        -I"'$MIPS_NATIVE_TOPSRC'"/include \
        $args -x assembler-with-cpp -E -P "'$src'" -o "'$tmp'"
fi

exec "$MIPS_NATIVE_AS" "$as_endian_flag" "$as_cpu_flag" -o "$out" "$tmp"
