#!/bin/sh
#
# Host-side smoke test for the RetroBSD/N64 a.out toolchain.
#
# This validates the native host tools used to produce target a.out objects:
#   - as writes big-endian RMAGIC objects;
#   - ld -r preserves relocatable a.out;
#   - ar creates archives that our ranlib can index;
#   - ld can resolve an undefined symbol from a ranlib-indexed archive;
#   - nm, size, and strip read/write the same big-endian a.out objects.
#

if test $# -ne 7; then
	echo "usage: $0 /path/to/as /path/to/ld /path/to/ar /path/to/ranlib /path/to/nm /path/to/size /path/to/strip" >&2
	exit 2
fi

as_bin=$1
ld_bin=$2
ar_bin=$3
ranlib_bin=$4
nm_bin=$5
size_bin=$6
strip_bin=$7

for tool in "$as_bin" "$ld_bin" "$ar_bin" "$ranlib_bin" "$nm_bin" "$size_bin" "$strip_bin"; do
	if test ! -x "$tool"; then
		echo "smoke-aout-toolchain: not executable: $tool" >&2
		exit 2
	fi
done

tmpdir=${TMPDIR:-/tmp}/n64-aout-toolchain.$$
rm -rf "$tmpdir"
mkdir "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' 0 1 2 3 15

main_s=$tmpdir/main.s
foo_s=$tmpdir/foo.s
chain_main_s=$tmpdir/chain-main.s
chain_mid_s=$tmpdir/chain-mid.s
chain_leaf_s=$tmpdir/chain-leaf.s
main_o=$tmpdir/main.o
foo_o=$tmpdir/foo.o
chain_main_o=$tmpdir/chain-main.o
chain_mid_o=$tmpdir/chain-mid.o
chain_leaf_o=$tmpdir/chain-leaf.o
partial_o=$tmpdir/partial.o
libfoo=$tmpdir/libfoo.a
libchain=$tmpdir/libchain.a
app=$tmpdir/app
app_chain=$tmpdir/app-chain
stripped=$tmpdir/app.stripped
list_before=$tmpdir/list-before
list_after=$tmpdir/list-after
chain_list_after=$tmpdir/chain-list-after
nm_main=$tmpdir/nm-main
size_main=$tmpdir/size-main

cat > "$main_s" <<'EOF'
.text
.set noreorder
.globl start
start:
	jal foo
	nop
	jr $31
	nop
EOF

cat > "$foo_s" <<'EOF'
.text
.set noreorder
.globl foo
foo:
	addiu $2,$0,42
	jr $31
	nop
EOF

cat > "$chain_main_s" <<'EOF'
.text
.set noreorder
.globl start
start:
	jal mid
	nop
	jr $31
	nop
EOF

cat > "$chain_mid_s" <<'EOF'
.text
.set noreorder
.globl mid
mid:
	jal leaf
	nop
	jr $31
	nop
EOF

cat > "$chain_leaf_s" <<'EOF'
.text
.set noreorder
.globl leaf
leaf:
	addiu $2,$0,7
	jr $31
	nop
EOF

be32()
{
	od -An -tx1 -j "$2" -N 4 "$1" | tr -d '[:space:]'
}

check_exec()
{
	file=$1
	want_magic=$2
	want_text=$3
	got_magic=`be32 "$file" 0`
	got_text=`be32 "$file" 4`
	if test "$got_magic" != "$want_magic"; then
		echo "smoke-aout-toolchain: $file magic $got_magic != $want_magic" >&2
		exit 1
	fi
	if test "$got_text" != "$want_text"; then
		echo "smoke-aout-toolchain: $file text $got_text != $want_text" >&2
		exit 1
	fi
}

check_field()
{
	file=$1
	off=$2
	want=$3
	got=`be32 "$file" "$off"`
	if test "$got" != "$want"; then
		echo "smoke-aout-toolchain: $file field@$off $got != $want" >&2
		exit 1
	fi
}

"$as_bin" -EB -mips3 -march=vr4300 -o "$main_o" "$main_s" || exit 1
"$as_bin" -EB -mips3 -march=vr4300 -o "$foo_o" "$foo_s" || exit 1
"$as_bin" -EB -mips3 -march=vr4300 -o "$chain_main_o" "$chain_main_s" || exit 1
"$as_bin" -EB -mips3 -march=vr4300 -o "$chain_mid_o" "$chain_mid_s" || exit 1
"$as_bin" -EB -mips3 -march=vr4300 -o "$chain_leaf_o" "$chain_leaf_s" || exit 1

check_exec "$main_o" 00000106 00000010
check_exec "$foo_o" 00000106 00000010

"$nm_bin" -p "$main_o" > "$nm_main" || exit 1
if ! grep ' T start' "$nm_main" >/dev/null; then
	echo "smoke-aout-toolchain: nm did not report start text symbol" >&2
	cat "$nm_main" >&2
	exit 1
fi
if ! grep ' U foo' "$nm_main" >/dev/null; then
	echo "smoke-aout-toolchain: nm did not report foo undefined symbol" >&2
	cat "$nm_main" >&2
	exit 1
fi

"$size_bin" "$main_o" > "$size_main" || exit 1
if ! grep '16	0	0	16	10' "$size_main" >/dev/null; then
	echo "smoke-aout-toolchain: size output is unexpected" >&2
	cat "$size_main" >&2
	exit 1
fi

"$ld_bin" -EB -r -o "$partial_o" "$main_o" "$foo_o" || exit 1
check_exec "$partial_o" 00000106 00000020

"$ar_bin" qc "$libfoo" "$foo_o" || exit 1
"$ar_bin" t "$libfoo" > "$list_before" || exit 1
if test "`cat "$list_before"`" != "foo.o"; then
	echo "smoke-aout-toolchain: ar table before ranlib is unexpected" >&2
	cat "$list_before" >&2
	exit 1
fi

"$ranlib_bin" "$libfoo" || exit 1
"$ar_bin" t "$libfoo" > "$list_after" || exit 1
if test "`sed -n '1p' "$list_after"`" != "__.SYMDEF"; then
	echo "smoke-aout-toolchain: ranlib did not add __.SYMDEF" >&2
	cat "$list_after" >&2
	exit 1
fi
if test "`sed -n '2p' "$list_after"`" != "foo.o"; then
	echo "smoke-aout-toolchain: archive member order after ranlib is unexpected" >&2
	cat "$list_after" >&2
	exit 1
fi

"$ld_bin" -EB -e start -o "$app" "$main_o" "$libfoo" || exit 1
check_exec "$app" 00000107 00000020
check_field "$app" 28 00400000

"$ar_bin" qc "$libchain" "$chain_leaf_o" "$chain_mid_o" || exit 1
"$ranlib_bin" "$libchain" || exit 1
"$ar_bin" t "$libchain" > "$chain_list_after" || exit 1
if test "`sed -n '1p' "$chain_list_after"`" != "__.SYMDEF"; then
	echo "smoke-aout-toolchain: chained ranlib did not add __.SYMDEF" >&2
	cat "$chain_list_after" >&2
	exit 1
fi
if test "`sed -n '2p' "$chain_list_after"`" != "chain-leaf.o"; then
	echo "smoke-aout-toolchain: chained archive order lost leaf object" >&2
	cat "$chain_list_after" >&2
	exit 1
fi
if test "`sed -n '3p' "$chain_list_after"`" != "chain-mid.o"; then
	echo "smoke-aout-toolchain: chained archive order lost mid object" >&2
	cat "$chain_list_after" >&2
	exit 1
fi
"$ld_bin" -EB -e start -o "$app_chain" "$chain_main_o" "$libchain" || exit 1
check_exec "$app_chain" 00000107 00000030
check_field "$app_chain" 28 00400000

cp "$app" "$stripped"
"$strip_bin" "$stripped" || exit 1
check_exec "$stripped" 00000107 00000020
check_field "$stripped" 24 00000000
check_field "$stripped" 28 00400000

echo "smoke-aout-toolchain: ok"
