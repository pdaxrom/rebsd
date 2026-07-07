#!/bin/sh
set -e

if [ $# -ne 6 ] && [ $# -ne 7 ] && [ $# -ne 8 ] && [ $# -ne 9 ] && [ $# -ne 10 ]; then
	echo "usage: $0 topsrc builddir prefix include-dir rebsd-as rebsd-ld [cpu] [float-abi] [endian] [ldscript]" >&2
	exit 2
fi

topsrc=$1
builddir=$2
prefix=$3
incdir=$4
as=$5
ld=$6
cpu=${7:-vr4300}
float_abi=${8:-hard}
endian=${9:-big}
ldscript=${10:-}
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
case "$float_abi" in
hard)
	float_cflags="-DMIPS_SOFT_FLOAT_DEFAULT=0"
	;;
soft)
	float_cflags="-DSOFTFLOAT -DMIPS_SOFT_FLOAT_DEFAULT=1"
	;;
*)
	echo "unsupported PCC float ABI default: $float_abi" >&2
	exit 2
	;;
esac
case "$endian" in
big)
	target=mips-rebsd
	endian_cflags="-DTARGET_BIG_ENDIAN=1"
	ldscript_name=elf32-bigmips.ld
	;;
little)
	target=mipsel-rebsd
	endian_cflags="-DTARGET_LITTLE_ENDIAN=1"
	ldscript_name=elf32-littlemips.ld
	;;
*)
	echo "unsupported PCC endian default: $endian" >&2
	exit 2
	;;
esac
target_root=$prefix/$target
target_incdir=$target_root/include
target_libdir=$target_root/lib
target_ldscript_dir=$target_libdir/ldscripts
target_ldscript=$target_ldscript_dir/$ldscript_name
target_softfloat_libdir=$target_libdir/softfloat
target_bindir=$prefix/bin
target_as=$target_bindir/$target-as
target_ld=$target_bindir/$target-ld

test -x "$pcc_src/configure"
test -d "$incdir"
test -x "$as"
test -x "$ld"
if [ -n "$ldscript" ]; then
	test -f "$ldscript"
fi

mkdir -p "$builddir" "$target_bindir"
rm -rf "$target_incdir" "$target_libdir"
mkdir -p "$target_incdir" "$target_libdir" "$target_softfloat_libdir"
cp -pR "$incdir"/. "$target_incdir"/
if [ -n "$ldscript" ]; then
	mkdir -p "$target_ldscript_dir"
	cp -p "$ldscript" "$target_ldscript"
fi
cp -p "$as" "$target_as"
cp -p "$ld" "$target_ld"
tool_src_dir=$(dirname "$as")
for tool in aout ar ranlib nm size strip; do
	if [ -x "$tool_src_dir/$tool" ]; then
		cp -p "$tool_src_dir/$tool" "$target_bindir/$target-$tool"
		ln -sf "$target-$tool" "$target_bindir/$tool"
	fi
done
ln -sf "$target-as" "$target_bindir/as"
ln -sf "$target-ld" "$target_bindir/ld"
cd "$builddir"

CFLAGS="${CFLAGS:-} $endian_cflags -DMIPS_CPU_DEFAULT=$cpu_default $float_cflags" \
"$pcc_src/configure" \
	--target="$target" \
	--prefix="$prefix" \
	--with-incdir="$target_incdir" \
	--with-libdir="$target_libdir" \
	--with-assembler="$target_as" \
	--with-linker="$target_ld"

${MAKE:-make} -B -C cc/cc all
${MAKE:-make} -B -C cc/cpp all
${MAKE:-make} -B -C cc/ccom all
${MAKE:-make} -C cc/cc install
${MAKE:-make} -C cc/cpp install
${MAKE:-make} -C cc/ccom install

pcc=$target_bindir/$target-pcc
test -x "$pcc"
ln -sf "$target-pcc" "$target_bindir/pcc"
ln -sf "$target-pcc" "$target_bindir/cc"
ln -sf "$target-pcc" "$target_bindir/$target-cc"
ln -sf "$target-pcpp" "$target_bindir/cpp"

tmp=${TMPDIR:-/tmp}/rebsd-host-portablecc.$$
trap 'rm -f "$tmp.c" "$tmp.s" "$tmp.o" "$tmp.macros"' 0 1 2 3 15

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
test -s "$tmp.o"

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
"$pcc" -E -dM "$tmp.c" > "$tmp.macros"
if [ "$float_abi" = soft ]; then
	grep '^#define __mips_soft_float' "$tmp.macros" >/dev/null
else
	grep '^#define __mips_hard_float' "$tmp.macros" >/dev/null
fi
if [ "$endian" = big ]; then
	grep '^#define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__$' "$tmp.macros" >/dev/null
	grep '^#define __MIPSEB__' "$tmp.macros" >/dev/null
else
	grep '^#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__$' "$tmp.macros" >/dev/null
	grep '^#define __MIPSEL__' "$tmp.macros" >/dev/null
fi

echo "smoke-host-portablecc: ok"
