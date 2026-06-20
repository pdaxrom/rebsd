#!/bin/sh
#
# Host-side smoke test for the RetroBSD/N64 a.out toolchain.
#
# This validates the native host tools used to produce target a.out objects:
#   - as writes big-endian RMAGIC objects;
#   - ld -r preserves relocatable a.out;
#   - ar creates archives that our ranlib can index;
#   - ld can resolve an undefined symbol from a ranlib-indexed archive.
#

if test $# -ne 4; then
	echo "usage: $0 /path/to/as /path/to/ld /path/to/ar /path/to/ranlib" >&2
	exit 2
fi

as_bin=$1
ld_bin=$2
ar_bin=$3
ranlib_bin=$4

for tool in "$as_bin" "$ld_bin" "$ar_bin" "$ranlib_bin"; do
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
main_o=$tmpdir/main.o
foo_o=$tmpdir/foo.o
partial_o=$tmpdir/partial.o
libfoo=$tmpdir/libfoo.a
app=$tmpdir/app
list_before=$tmpdir/list-before
list_after=$tmpdir/list-after

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

"$as_bin" -EB -mips3 -march=vr4300 -o "$main_o" "$main_s" || exit 1
"$as_bin" -EB -mips3 -march=vr4300 -o "$foo_o" "$foo_s" || exit 1

check_exec "$main_o" 00000106 00000010
check_exec "$foo_o" 00000106 0000000c

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

echo "smoke-aout-toolchain: ok"
