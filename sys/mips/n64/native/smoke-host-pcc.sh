#!/bin/sh
set -e

if [ $# -ne 6 ]; then
	echo "usage: $0 host-cpp host-ccom retrobsd-as retrobsd-ld include-dir source.c" >&2
	exit 2
fi

cpp=$1
ccom=$2
as=$3
ld=$4
incdir=$5
src=$6

tmp=${TMPDIR:-/tmp}/n64-host-pcc.$$
trap 'rm -f "$tmp.i" "$tmp.s" "$tmp.o" "$tmp.ro"' 0 1 2 3 15

"$cpp" \
	-D__PCC_MINOR__=9 \
	-D__PCC_MINORMINOR__=9 \
	-D__GNUC__=4 \
	-D__GNUC_MINOR__=3 \
	-D__GNUC_PATCHLEVEL__=1 \
	-D__GNUC_STDC_INLINE__=1 \
	-D__REGISTER_PREFIX__= \
	-D__USER_LABEL_PREFIX__= \
	-D__PCC__=0 \
	-D__unix__ \
	-D__BSD__ \
	-D__RETROBSD__ \
	-D__STDC_ISO_10646__=200009L \
	-D__WCHAR_TYPE__="short unsigned int" \
	-D__SIZEOF_WCHAR_T__=2 \
	-D__WCHAR_MAX__=65535U \
	-D__WINT_TYPE__="unsigned int" \
	-D__SIZE_TYPE__="unsigned long" \
	-D__PTRDIFF_TYPE__="long int" \
	-D__SIZEOF_WINT_T__=4 \
	-D__mips__ \
	-S "$incdir" "$src" "$tmp.i"

"$ccom" "$tmp.i" "$tmp.s"
"$as" -EB -mips3 -march=vr4300 -o "$tmp.o" "$tmp.s"
"$ld" -r -o "$tmp.ro" "$tmp.o"

echo "smoke-host-pcc: ok"
