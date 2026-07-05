#!/bin/sh
set -e

if [ $# -ne 7 ] && [ $# -ne 8 ]; then
	echo "usage: $0 topsrc builddir prefix include-dir lib-dir rebsd-as rebsd-ld [cpu]" >&2
	exit 2
fi

topsrc=$1
builddir=$2
prefix=$3
incdir=$4
libdir=$5
as=$6
ld=$7
cpu=${8:-vr4300}
pcc_src=$topsrc/src/dev/pcc/pcc

case "$cpu" in
vr4300)
	cpu_default=MIPS_CPU_VR4300
	;;
mips32r2)
	cpu_default=MIPS_CPU_MIPS32R2
	;;
*)
	echo "unsupported PCC CPU default: $cpu" >&2
	exit 2
	;;
esac

test -x "$pcc_src/configure"
test -d "$incdir"
test -d "$libdir"
test -x "$as"
test -x "$ld"

mkdir -p "$builddir" "$prefix"
cd "$builddir"

CFLAGS="${CFLAGS:-} -DMIPS_CPU_DEFAULT=$cpu_default" \
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
trap 'rm -f "$tmp" "$tmp.c" "$tmp.s" "$tmp.o" "$tmp.macros"' 0 1 2 3 15

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

cat > "$tmp.c" <<'EOF'
struct sd {
	char c;
	double d;
};
typedef char check_sd_size[(sizeof(struct sd) == EXPECT_SIZE) ? 1 : -1];
EOF

"$pcc" -march=vr4300 -DEXPECT_SIZE=16 -c -o "$tmp.o" "$tmp.c"
"$pcc" -march=mips32r2 -DEXPECT_SIZE=12 -c -o "$tmp.o" "$tmp.c"
"$pcc" -march=mips32r2 -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips 32$' "$tmp.macros" >/dev/null
grep '^#define __mips32r2' "$tmp.macros" >/dev/null
"$pcc" -march=vr4300 -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips 3$' "$tmp.macros" >/dev/null
grep '^#define __vr4300__' "$tmp.macros" >/dev/null

echo "smoke-host-portablecc: ok"
