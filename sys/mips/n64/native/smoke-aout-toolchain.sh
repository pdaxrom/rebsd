#!/bin/sh
#
# Host-side smoke test for the ReBSD/N64 a.out toolchain.
#
# This validates the native host tools used to produce target a.out objects:
#   - as writes big-endian RMAGIC objects;
#   - ld -r preserves relocatable a.out;
#   - ar creates archives that our ranlib can index;
#   - ld can resolve an undefined symbol from a ranlib-indexed archive;
#   - nm, size, and strip read/write the same big-endian a.out objects;
#   - when GNU as is supplied, ReBSD as emits the same VR4300 text bytes.
#   - the ELF linker evaluates shifts in linker-script layout expressions.
#

if test $# -ne 7 && test $# -ne 8; then
	echo "usage: $0 /path/to/as /path/to/ld /path/to/ar /path/to/ranlib /path/to/nm /path/to/size /path/to/strip [/path/to/gnu-as]" >&2
	exit 2
fi

as_bin=$1
ld_bin=$2
ar_bin=$3
ranlib_bin=$4
nm_bin=$5
size_bin=$6
strip_bin=$7
gnu_as=
gnu_objcopy=
if test $# -eq 8; then
	gnu_as=$8
fi

for tool in "$as_bin" "$ld_bin" "$ar_bin" "$ranlib_bin" "$nm_bin" "$size_bin" "$strip_bin"; do
	if test ! -x "$tool"; then
		echo "smoke-aout-toolchain: not executable: $tool" >&2
		exit 2
	fi
done
if test -n "$gnu_as"; then
	command -v "$gnu_as" >/dev/null 2>&1
	if test $? -ne 0; then
		echo "smoke-aout-toolchain: GNU as not found: $gnu_as" >&2
		exit 2
	fi
	case "$gnu_as" in
	*/mips64-elf-as)
		gnu_objcopy=`dirname "$gnu_as"`/mips64-elf-objcopy
		;;
	mips64-elf-as)
		gnu_objcopy=mips64-elf-objcopy
		;;
	*as)
		gnu_objcopy=`echo "$gnu_as" | sed 's/as$/objcopy/'`
		;;
	*)
		gnu_objcopy=mips64-elf-objcopy
		;;
	esac
	command -v "$gnu_objcopy" >/dev/null 2>&1
	if test $? -ne 0; then
		echo "smoke-aout-toolchain: GNU objcopy not found: $gnu_objcopy" >&2
		exit 2
	fi
fi

tmpdir=${TMPDIR:-/tmp}/n64-aout-toolchain.$$
rm -rf "$tmpdir"
mkdir "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' 0 1 2 3 15

main_s=$tmpdir/main.s
foo_s=$tmpdir/foo.s
data_s=$tmpdir/data.s
end_s=$tmpdir/end.s
chain_main_s=$tmpdir/chain-main.s
chain_mid_s=$tmpdir/chain-mid.s
chain_leaf_s=$tmpdir/chain-leaf.s
hilo_s=$tmpdir/hilo.s
main_o=$tmpdir/main.o
foo_o=$tmpdir/foo.o
data_o=$tmpdir/data.o
end_o=$tmpdir/end.o
chain_main_o=$tmpdir/chain-main.o
chain_mid_o=$tmpdir/chain-mid.o
chain_leaf_o=$tmpdir/chain-leaf.o
hilo_o=$tmpdir/hilo.o
partial_o=$tmpdir/partial.o
libfoo=$tmpdir/libfoo.a
libchain=$tmpdir/libchain.a
app=$tmpdir/app
app_data=$tmpdir/app-data
app_end=$tmpdir/app-end
app_chain=$tmpdir/app-chain
app_hilo=$tmpdir/app-hilo
stripped=$tmpdir/app.stripped
list_before=$tmpdir/list-before
list_after=$tmpdir/list-after
chain_list_after=$tmpdir/chain-list-after
nm_main=$tmpdir/nm-main
size_main=$tmpdir/size-main
gnu_o=$tmpdir/gnu.o
gnu_text=$tmpdir/gnu.text
retro_text=$tmpdir/retro.text
gnu_log=$tmpdir/gnu.log
objcopy_log=$tmpdir/objcopy.log
elf_s=$tmpdir/elf.s
elf_o=$tmpdir/elf.o
elf_ld=$tmpdir/elf.ld
elf_app=$tmpdir/elf-app

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

cat > "$data_s" <<'EOF'
.text
.set noreorder
.globl start
start:
	jr $31
	nop
.data
.globl dataptr
dataptr:
	.word start
EOF

cat > "$end_s" <<'EOF'
.text
.set noreorder
.globl start
start:
	la $2,end
	la $3,edata
	la $4,etext
	la $5,_end
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

cat > "$hilo_s" <<'EOF'
.text
.set noreorder
.globl start
start:
	lui $1,%hi(big+0x748)
	sw $2,%lo(big+0x748)($1)
	jr $31
	nop
.bss
	.space 0x77f8
big:
	.space 0x800
EOF

cat > "$elf_s" <<'EOF'
.text
.globl _start
_start:
	nop
.data
.globl test_data
test_data:
	.word 1
EOF

cat > "$elf_ld" <<'EOF'
OUTPUT_FORMAT("elf32-bigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)
PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS
{
	. = 0x00400000;
	.text : { *(.text) } :text
	. = ALIGN((1 << 12));
	. = . + ((8 >> 3) - 1);
	.data : { *(.data) } :data
}
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

check_gnu_text()
{
	retro_o=$1
	src=$2
	name=$3

	if test -z "$gnu_as"; then
		return 0
	fi

	rm -f "$gnu_o" "$gnu_text" "$retro_text" "$gnu_log" "$objcopy_log"
	"$gnu_as" -EB -mips3 -march=vr4300 -o "$gnu_o" "$src" > "$gnu_log" 2>&1
	if test $? -ne 0; then
		echo "smoke-aout-toolchain: GNU as rejected $name" >&2
		cat "$gnu_log" >&2
		exit 1
	fi
	"$gnu_objcopy" -O binary -j .text "$gnu_o" "$gnu_text" \
	    > "$objcopy_log" 2>&1
	if test $? -ne 0; then
		echo "smoke-aout-toolchain: GNU objcopy failed for $name" >&2
		cat "$objcopy_log" >&2
		exit 1
	fi
	gnu_size=`wc -c < "$gnu_text" | tr -d '[:space:]'`
	dd if="$retro_o" of="$retro_text" bs=1 skip=32 count="$gnu_size" \
	    > /dev/null 2>&1
	if test $? -ne 0; then
		echo "smoke-aout-toolchain: cannot extract RetroBSD text for $name" >&2
		exit 1
	fi
	if ! cmp -s "$retro_text" "$gnu_text"; then
		echo "smoke-aout-toolchain: text bytes differ from GNU as for $name" >&2
		exit 1
	fi
}

"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$main_o" "$main_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$foo_o" "$foo_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$data_o" "$data_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$end_o" "$end_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$chain_main_o" "$chain_main_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$chain_mid_o" "$chain_mid_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$chain_leaf_o" "$chain_leaf_s" || exit 1
"$as_bin" --aout -EB -mips3 -march=vr4300 -o "$hilo_o" "$hilo_s" || exit 1

check_gnu_text "$main_o" "$main_s" main
check_gnu_text "$foo_o" "$foo_s" foo
check_gnu_text "$data_o" "$data_s" data
check_gnu_text "$end_o" "$end_s" end
check_gnu_text "$chain_main_o" "$chain_main_s" chain-main
check_gnu_text "$chain_mid_o" "$chain_mid_s" chain-mid
check_gnu_text "$chain_leaf_o" "$chain_leaf_s" chain-leaf

if test -n "$gnu_as"; then
	"$gnu_as" -EB -mips3 -march=vr4300 -o "$elf_o" "$elf_s" || exit 1
	"$ld_bin" --elf -EB -T "$elf_ld" -o "$elf_app" "$elf_o" || exit 1
	check_field "$elf_app" 0 7f454c46
	check_field "$elf_app" 84 00000001
	check_field "$elf_app" 92 00401000
	check_field "$elf_app" 112 00001000
fi

check_exec "$main_o" 00000106 00000010
check_exec "$foo_o" 00000106 00000010
check_exec "$data_o" 00000106 00000008
check_field "$data_o" 8 00000004

"$nm_bin" -pEB "$main_o" > "$nm_main" || exit 1
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

"$size_bin" -EB "$main_o" > "$size_main" || exit 1
if ! grep '16	0	0	16	10' "$size_main" >/dev/null; then
	echo "smoke-aout-toolchain: size output is unexpected" >&2
	cat "$size_main" >&2
	exit 1
fi

"$ld_bin" --aout -EB -r -o "$partial_o" "$main_o" "$foo_o" || exit 1
check_exec "$partial_o" 00000106 00000020

"$ld_bin" --aout -EB -e start -o "$app_data" "$data_o" || exit 1
check_exec "$app_data" 00000107 00000010
check_field "$app_data" 8 00000008
check_field "$app_data" 28 00400000
check_field "$app_data" 48 00400000

"$ld_bin" --aout -EB -e start -o "$app_end" "$end_o" || exit 1
check_exec "$app_end" 00000107 00000030

"$ld_bin" --aout -EB -e start -o "$app_hilo" "$hilo_o" || exit 1
check_exec "$app_hilo" 00000107 00000010
check_field "$app_hilo" 32 3c010040
check_field "$app_hilo" 36 ac227f50

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

"$ld_bin" --aout -EB -e start -o "$app" "$main_o" "$libfoo" || exit 1
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
"$ld_bin" --aout -EB -e start -o "$app_chain" "$chain_main_o" "$libchain" || exit 1
check_exec "$app_chain" 00000107 00000030
check_field "$app_chain" 28 00400000

cp "$app" "$stripped"
"$strip_bin" -EB "$stripped" || exit 1
check_exec "$stripped" 00000107 00000020
check_field "$stripped" 24 00000000
check_field "$stripped" 28 00400000

echo "smoke-aout-toolchain: ok"
