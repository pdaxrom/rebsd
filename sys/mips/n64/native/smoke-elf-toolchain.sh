#!/bin/sh
# Host-side smoke test for the ReBSD ELF32/MIPS toolchain.

set -e

if test $# -ne 7 && test $# -ne 8; then
	echo "usage: $0 as ld ar ranlib nm size strip [gnu-as]" >&2
	exit 2
fi

as_bin=$1
ld_bin=$2
ar_bin=$3
ranlib_bin=$4
nm_bin=$5
size_bin=$6
strip_bin=$7
gnu_as=${8:-}

for tool in "$as_bin" "$ld_bin" "$ar_bin" "$ranlib_bin" \
    "$nm_bin" "$size_bin" "$strip_bin"; do
	test -x "$tool" || {
		echo "smoke-elf-toolchain: not executable: $tool" >&2
		exit 2
	}
done

gnu_objcopy=
if test -n "$gnu_as"; then
	command -v "$gnu_as" >/dev/null 2>&1 || {
		echo "smoke-elf-toolchain: GNU as not found: $gnu_as" >&2
		exit 2
	}
	case "$gnu_as" in
	*/mips64-elf-as) gnu_objcopy=`dirname "$gnu_as"`/mips64-elf-objcopy ;;
	mips64-elf-as) gnu_objcopy=mips64-elf-objcopy ;;
	*as) gnu_objcopy=`echo "$gnu_as" | sed 's/as$/objcopy/'` ;;
	*) gnu_objcopy=mips64-elf-objcopy ;;
	esac
	command -v "$gnu_objcopy" >/dev/null 2>&1 || {
		echo "smoke-elf-toolchain: GNU objcopy not found: $gnu_objcopy" >&2
		exit 2
	}
fi

tmpdir=${TMPDIR:-/tmp}/n64-elf-toolchain.$$
rm -rf "$tmpdir"
mkdir "$tmpdir"
trap 'rm -rf "$tmpdir"' 0 1 2 3 15

main_s=$tmpdir/main.s
foo_s=$tmpdir/foo.s
main_o=$tmpdir/main.o
foo_o=$tmpdir/foo.o
partial_o=$tmpdir/partial.o
libfoo=$tmpdir/libfoo.a
app=$tmpdir/app
stripped=$tmpdir/app.stripped
linker=$tmpdir/user.ld

cat > "$main_s" <<'EOF'
.text
.set noreorder
.globl _start
_start:
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

cat > "$linker" <<'EOF'
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
	. = ALIGN(0x1000);
	.data : { *(.data) *(.rodata) } :data
	.bss : { *(.bss) *(COMMON) } :data
}
EOF

hex_field()
{
	od -An -tx1 -j "$2" -N "$3" "$1" | tr -d '[:space:]'
}

check_elf()
{
	file=$1
	want_type=$2
	test "`hex_field "$file" 0 4`" = 7f454c46 || {
		echo "smoke-elf-toolchain: $file has no ELF magic" >&2
		exit 1
	}
	test "`hex_field "$file" 4 2`" = 0102 || {
		echo "smoke-elf-toolchain: $file is not ELF32 big-endian" >&2
		exit 1
	}
	test "`hex_field "$file" 16 2`" = "$want_type" || {
		echo "smoke-elf-toolchain: $file has unexpected ELF type" >&2
		exit 1
	}
	test "`hex_field "$file" 18 2`" = 0008 || {
		echo "smoke-elf-toolchain: $file is not ELF32/MIPS" >&2
		exit 1
	}
}

compare_text()
{
	test -n "$gnu_as" || return 0
	src=$1
	retro=$2
	name=$3
	gnu_o=$tmpdir/$name.gnu.o
	gnu_text=$tmpdir/$name.gnu.text
	retro_text=$tmpdir/$name.retro.text
	retro_prefix=$tmpdir/$name.retro.prefix
	"$gnu_as" -EB -mips3 -march=vr4300 -o "$gnu_o" "$src"
	"$gnu_objcopy" -O binary -j .text "$gnu_o" "$gnu_text"
	"$gnu_objcopy" -O binary -j .text "$retro" "$retro_text"
	gnu_size=`wc -c < "$gnu_text" | tr -d '[:space:]'`
	retro_size=`wc -c < "$retro_text" | tr -d '[:space:]'`
	test "$retro_size" -ge "$gnu_size" || {
		echo "smoke-elf-toolchain: .text is shorter than GNU as for $name" >&2
		exit 1
	}
	dd if="$retro_text" of="$retro_prefix" bs=1 count="$gnu_size" 2>/dev/null
	cmp "$gnu_text" "$retro_prefix" || {
		echo "smoke-elf-toolchain: .text differs from GNU as for $name" >&2
		exit 1
	}
	if test "$retro_size" -gt "$gnu_size"; then
		padding=`dd if="$retro_text" bs=1 skip="$gnu_size" 2>/dev/null |
		    od -An -tx1 | tr -d '[:space:]0'`
		test -z "$padding" || {
			echo "smoke-elf-toolchain: non-zero .text padding for $name" >&2
			exit 1
		}
	fi
}

"$as_bin" --elf -EB -mips3 -march=vr4300 -o "$main_o" "$main_s"
"$as_bin" --elf -EB -mips3 -march=vr4300 -o "$foo_o" "$foo_s"
check_elf "$main_o" 0001
check_elf "$foo_o" 0001
compare_text "$main_s" "$main_o" main
compare_text "$foo_s" "$foo_o" foo

"$nm_bin" -p "$main_o" > "$tmpdir/nm-main"
grep ' T _start' "$tmpdir/nm-main" >/dev/null
grep ' U foo' "$tmpdir/nm-main" >/dev/null
"$size_bin" "$main_o" > "$tmpdir/size-main"
grep '^16[[:space:]]' "$tmpdir/size-main" >/dev/null

"$ld_bin" --elf -EB -r -o "$partial_o" "$main_o" "$foo_o"
check_elf "$partial_o" 0001

"$ar_bin" qc "$libfoo" "$foo_o"
"$ranlib_bin" "$libfoo"
"$ar_bin" t "$libfoo" > "$tmpdir/archive-list"
grep '^foo.o$' "$tmpdir/archive-list" >/dev/null

"$ld_bin" --elf -EB -T "$linker" -o "$app" "$main_o" "$libfoo"
check_elf "$app" 0002
test "`hex_field "$app" 24 4`" = 00400000

cp "$app" "$stripped"
before=`wc -c < "$stripped" | tr -d '[:space:]'`
"$strip_bin" "$stripped"
after=`wc -c < "$stripped" | tr -d '[:space:]'`
check_elf "$stripped" 0002
test "$after" -lt "$before"
test "`hex_field "$stripped" 32 4`" = 00000000

echo "smoke-elf-toolchain: ok"
