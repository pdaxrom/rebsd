#!/bin/sh
set -e

if [ "$#" -ne 5 ]; then
	echo "usage: $0 topsrc workdir include-dir runtime-dir ldscript" >&2
	exit 2
fi

topsrc=$1
workdir=$2
incdir=$3
runtime_dir=$4
ldscript=$5
hostcc=${HOSTCC:-cc}
make=${REBSD_REAL_MAKE:-${MAKE:-make}}
target=i386-rebsd
version=1.2.0.DEVEL
tools=$workdir/tools
hostinc=$tools/include
pcc_src=$topsrc/src/dev/pcc/pcc
pcc_build=$workdir/host-pcc-build
pcc_prefix=$workdir/host-pcc
target_root=$pcc_prefix/$target
target_lib=$target_root/lib
cross_pcc=$pcc_prefix/bin/$target-pcc
libpcc_build=$workdir/libpcc-build
syslib=$workdir/syslib
native_build=$workdir/native-build
native_out=$workdir/native

for path in "$topsrc" "$incdir" "$runtime_dir"; do
	test -d "$path"
done
test -f "$ldscript"
test -x "$pcc_src/configure"

rm -rf "$workdir"
mkdir -p "$tools" "$pcc_build" "$target_root/include" \
	"$target_lib/ldscripts" "$pcc_prefix/bin"
"$topsrc/tools/build/mkhost-tool-includes.sh" "$topsrc" "$hostinc"

$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-o "$tools/as" "$topsrc/src/cmd/as-i386/as.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-o "$tools/ld" "$topsrc/src/cmd/ld/ld.c" \
	"$topsrc/src/cmd/aoutio.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-I"$topsrc/src/cmd/ar" -o "$tools/ar" \
	"$topsrc/src/cmd/ar/append.c" "$topsrc/src/cmd/ar/ar.c" \
	"$topsrc/src/cmd/ar/archive.c" "$topsrc/src/cmd/ar/contents.c" \
	"$topsrc/src/cmd/ar/delete.c" "$topsrc/src/cmd/ar/extract.c" \
	"$topsrc/src/cmd/ar/misc.c" "$topsrc/src/cmd/ar/move.c" \
	"$topsrc/src/cmd/ar/print.c" "$topsrc/src/cmd/ar/replace.c" \
	"$topsrc/src/cmd/ar/strmode.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-I"$topsrc/src/cmd/ar" -o "$tools/ranlib" \
	"$topsrc/src/cmd/ranlib/ranlib.c" \
	"$topsrc/src/cmd/ar/archive.c" "$topsrc/src/cmd/aoutio.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-I"$topsrc/src/cmd/ar" -o "$tools/nm" \
	"$topsrc/src/cmd/nm/nm.c" "$topsrc/src/cmd/aoutio.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-o "$tools/size" "$topsrc/src/cmd/size.c" \
	"$topsrc/src/cmd/aoutio.c"
$hostcc -DCROSS -O2 -I"$hostinc" -I"$topsrc/src/cmd" \
	-o "$tools/strip" "$topsrc/src/cmd/strip.c" \
	"$topsrc/src/cmd/aoutio.c"

cp -pR "$incdir"/. "$target_root/include"/
cp -p "$ldscript" "$target_lib/ldscripts/elf32-i386.ld"
for tool in as ld ar ranlib nm size strip; do
	cp -p "$tools/$tool" "$pcc_prefix/bin/$target-$tool"
	ln -sf "$target-$tool" "$pcc_prefix/bin/$tool"
done

cd "$pcc_build"
CFLAGS="${CFLAGS:-} -DTARGET_LITTLE_ENDIAN=1 -DREBSD_TOOLCHAIN_ELF_DEFAULT" \
	"$pcc_src/configure" --target="$target" --prefix="$pcc_prefix" \
	--with-incdir="$target_root/include" --with-libdir="$target_lib" \
	--with-assembler="$pcc_prefix/bin/$target-as" \
	--with-linker="$pcc_prefix/bin/$target-ld"
"$make" -B -C cc/cc all
"$make" -B -C cc/cpp all
"$make" -B -C cc/ccom all
"$make" -C cc/cc install
"$make" -C cc/cpp install
"$make" -C cc/ccom install
ln -sf "$target-pcc" "$pcc_prefix/bin/pcc"
ln -sf "$target-pcc" "$pcc_prefix/bin/cc"
ln -sf "$target-pcpp" "$pcc_prefix/bin/cpp"
test -x "$cross_pcc"

mkdir -p "$libpcc_build"
libpcc_objects=
for name in cmpdi2 divdi3 fixdfdi fixsfdi fixunsdfdi fixunssfdi \
	floatdidf floatdisf floatunsdidf isinf_sign moddi3 muldi3 negdi2 \
	qdivrem ucmpdi2 udivdi3 umoddi3 cxmuldiv ashldi3 ashrdi3 \
	lshrdi3 _alloca unwind ssp signbit; do
	src=$topsrc/src/dev/pcc/pcc-libs/libpcc/$name.c
	obj=$libpcc_build/$name.o
	"$cross_pcc" -march=i686 -O -fno-builtin -fno-stack-protector \
		-Dos_rebsd -Dmach_i386 \
		-I"$topsrc/src/dev/pcc/pcc-libs/libpcc" \
		-I"$topsrc/src/dev/pcc/pcc-libs/libpcc/include" \
		-c -o "$obj" "$src"
	libpcc_objects="$libpcc_objects $obj"
done
"$tools/ar" r "$syslib.tmp" $libpcc_objects
"$tools/ranlib" "$syslib.tmp"
mkdir -p "$syslib"
mv "$syslib.tmp" "$syslib/libpcc.a"
cp -p "$runtime_dir/crt0.o" "$runtime_dir/libc.a" \
	"$runtime_dir/libm.a" "$syslib"/
cp -p "$syslib/crt0.o" "$syslib/libc.a" "$syslib/libm.a" \
	"$syslib/libpcc.a" "$target_lib"/

"$make" -f "$topsrc/src/dev/pcc/Makefile.native" \
	PCC_ARCH=i386 PCC_CPU=i686 TOPSRC="$topsrc" \
	BUILD="$native_build" OUT="$native_out" \
	TARGET_CC="$cross_pcc" TARGET_LD="$tools/ld" \
	LDSCRIPT="$ldscript" CRT0="$syslib/crt0.o" LIBDIR="$syslib" all

"$cross_pcc" -march=i686 -c -o "$workdir/host-smoke.o" \
	"$topsrc/sys/i386/pc/rootfs/root/pcc-smoke.c"
test -s "$workdir/host-smoke.o"
for tool in "$native_out/cc" "$native_out/cpp" "$native_out/ccom"; do
	test -x "$tool"
done
