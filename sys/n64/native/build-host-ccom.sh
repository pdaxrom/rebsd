#!/bin/sh
set -e

if [ $# -ne 3 ]; then
	echo "usage: $0 topsrc builddir output" >&2
	exit 2
fi

topsrc=$(cd "$1" && pwd)
builddir=$2
output=$3
src=$topsrc/src/cmd/ccom

HOSTCC=${HOSTCC:-cc}
YACC=${YACC:-byacc}
LEX=${LEX:-flex}

rm -rf "$builddir"
mkdir -p "$builddir" "$(dirname "$output")"

common_flags="-DGCC_COMPAT -DPCC_DEBUG -D_ISOC99_SOURCE -DYY_NO_INPUT"
common_flags="$common_flags -Dmach_mips -DHAVE_STRLCPY=1 -DHAVE_STRLCAT=1"
target_flags="-DTARGET_BIG_ENDIAN -DTARGET_VR4300 -DTARGET_NO_ABICALLS"
includes="-I$builddir -I$src -I$src/mip -I$src/arch-mips"
cflags="-O2 $common_flags $target_flags $includes -g -Wall"
cflags="$cflags -Wno-deprecated-non-prototype -fcommon -fno-strict-aliasing"
cflags="$cflags -Wno-strict-aliasing -Wno-unused-but-set-variable"

$HOSTCC -O2 $common_flags -I"$src" -I"$src/mip" -I"$src/arch-mips" \
	-DMKEXT -o "$builddir/mkext" \
	"$src/mip/mkext.c" "$src/arch-mips/table.c" "$src/mip/common.c"
(cd "$builddir" && ./mkext)

(cd "$builddir" && $YACC -d "$src/cgram.y" && \
	mv y.tab.c cgram.c && mv y.tab.h cgram.h)
$LEX -o "$builddir/scan.c" "$src/scan.l"

objects="optim.o pftn.o scan.o trees.o cgram.o inline.o symtabs.o"
objects="$objects gcc_compat.o init.o local.o code.o stabs.o builtins.o"
objects="$objects match.o reader.o optim2.o regs.o local2.o order.o table.o"
objects="$objects compat.o common.o main.o external.o"

for spec in \
	"optim:$src/optim.c" \
	"pftn:$src/pftn.c" \
	"scan:$builddir/scan.c" \
	"trees:$src/trees.c" \
	"cgram:$builddir/cgram.c" \
	"inline:$src/inline.c" \
	"symtabs:$src/symtabs.c" \
	"gcc_compat:$src/gcc_compat.c" \
	"init:$src/init.c" \
	"local:$src/arch-mips/local.c" \
	"code:$src/arch-mips/code.c" \
	"stabs:$src/stabs.c" \
	"builtins:$src/builtins.c" \
	"match:$src/mip/match.c" \
	"reader:$src/mip/reader.c" \
	"optim2:$src/mip/optim2.c" \
	"regs:$src/mip/regs.c" \
	"local2:$src/arch-mips/local2.c" \
	"order:$src/arch-mips/order.c" \
	"table:$src/arch-mips/table.c" \
	"compat:$src/mip/compat.c" \
	"common:$src/mip/common.c" \
	"main:$src/main.c" \
	"external:$builddir/external.c"
do
	name=${spec%%:*}
	source=${spec#*:}
	$HOSTCC $cflags -c -o "$builddir/$name.o" "$source"
done

set -- $objects
for object do
	link_objects="$link_objects $builddir/$object"
done

$HOSTCC -o "$output" $link_objects
