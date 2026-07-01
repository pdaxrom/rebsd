#!/bin/sh
set -e

if [ $# -ne 7 ]; then
	echo "usage: $0 topsrc builddir prefix include-dir lib-dir rebsd-as rebsd-ld" >&2
	exit 2
fi

topsrc=$1
builddir=$2
prefix=$3
incdir=$4
libdir=$5
as=$6
ld=$7
pcc_src=$topsrc/src/dev/pcc/pcc

test -x "$pcc_src/configure"
test -d "$incdir"
test -d "$libdir"
test -x "$as"
test -x "$ld"

mkdir -p "$builddir" "$prefix"
cd "$builddir"

"$pcc_src/configure" \
	--target=mips-rebsd \
	--prefix="$prefix" \
	--with-incdir="$incdir" \
	--with-libdir="$libdir" \
	--with-assembler="$as" \
	--with-linker="$ld"

${MAKE:-make} -B all
${MAKE:-make} install

pcc=$prefix/bin/mips-rebsd-pcc
test -x "$pcc"

tmp=${TMPDIR:-/tmp}/rebsd-host-portablecc.$$
trap 'rm -f "$tmp" "$tmp.c" "$tmp.s" "$tmp.o"' 0 1 2 3 15

cat > "$tmp.c" <<'EOF'
#include <stdio.h>

static int
add3(int a, int b, int c)
{
	return a + b + c;
}

int
main(void)
{
	int value = add3(1, 2, 3);

	printf("rebsd-pcc-smoke:%d\n", value);
	return value == 6 ? 0 : 1;
}
EOF

"$pcc" -S -o "$tmp.s" "$tmp.c"
"$pcc" -c -o "$tmp.o" "$tmp.c"
"$pcc" -o "$tmp" "$tmp.c"
test -s "$tmp"

echo "smoke-host-portablecc: ok"
