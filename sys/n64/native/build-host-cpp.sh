#!/bin/sh
set -e

if [ $# -ne 3 ]; then
	echo "usage: $0 topsrc builddir output" >&2
	exit 2
fi

topsrc=$(cd "$1" && pwd)
builddir=$2
output=$3
src=$topsrc/src/cmd/cpp

HOSTCC=${HOSTCC:-cc}
YACC=${YACC:-byacc}

rm -rf "$builddir"
mkdir -p "$builddir" "$(dirname "$output")"

cflags="-O2 -DGCC_COMPAT -DHAVE_CPP_VARARG_MACRO_GCC"
cflags="$cflags -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1"
cflags="$cflags -I$src -I$builddir -Wall"
cflags="$cflags -Wno-deprecated-non-prototype -Wno-unused-but-set-variable"

(cd "$builddir" && $YACC -d "$src/cpy.y")

$HOSTCC $cflags -o "$output" \
	"$src/cpp.c" "$builddir/y.tab.c" "$src/token.c" \
	"$src/compat.c" "$src/doprnt.c"
