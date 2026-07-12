#!/bin/sh
set -e

# RetroBSD recursive builds use an object-directory dispatcher as MAKE.  PCC's
# own generated Makefiles live outside that tree and must use the underlying
# make executable instead.
MAKE=${REBSD_REAL_MAKE:-${MAKE:-make}}
export MAKE

if [ $# -lt 6 ] || [ $# -gt 11 ]; then
	echo "usage: $0 topsrc builddir prefix include-dir rebsd-as rebsd-ld [cpu] [float-abi] [endian] [ldscript] [exec-format]" >&2
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
exec_format=${11:-elf}
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
	elf_endian=-EB
	ldscript_name=elf32-bigmips.ld
	;;
little)
	target=mipsel-rebsd
	endian_cflags="-DTARGET_LITTLE_ENDIAN=1"
	elf_endian=-EL
	ldscript_name=elf32-littlemips.ld
	;;
*)
	echo "unsupported PCC endian default: $endian" >&2
	exit 2
	;;
esac
case "$exec_format" in
elf)
	format_cflags="-DREBSD_TOOLCHAIN_ELF_DEFAULT"
	;;
aout)
	format_cflags=
	;;
*)
	echo "unsupported PCC executable format default: $exec_format" >&2
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

CFLAGS="${CFLAGS:-} $endian_cflags -DMIPS_CPU_DEFAULT=$cpu_default $float_cflags $format_cflags" \
"$pcc_src/configure" \
	--target="$target" \
	--prefix="$prefix" \
	--with-incdir="$target_incdir" \
	--with-libdir="$target_libdir" \
	--with-assembler="$target_as" \
	--with-linker="$target_ld"

"$MAKE" -B -C cc/cc all
"$MAKE" -B -C cc/cpp all
"$MAKE" -B -C cc/ccom all
"$MAKE" -C cc/cc install
"$MAKE" -C cc/cpp install
"$MAKE" -C cc/ccom install

pcc=$target_bindir/$target-pcc
test -x "$pcc"
ln -sf "$target-pcc" "$target_bindir/pcc"
ln -sf "$target-pcc" "$target_bindir/cc"
ln -sf "$target-pcc" "$target_bindir/$target-cc"
ln -sf "$target-pcpp" "$target_bindir/cpp"

tmp=${TMPDIR:-/tmp}/rebsd-host-portablecc.$$
trap 'rm -f "$tmp.c" "$tmp.s" "$tmp.o" "$tmp.macros" "$tmp.err" \
    "$tmp.default.s" "$tmp.noomit.s" \
    "$tmp.normal.s" "$tmp.stats.s" "$tmp.stats2.s" "$tmp.stats.off" \
    "$tmp.stats.log" "$tmp.stats2.log" "$tmp.ssa.s" "$tmp.ssa.log" \
    "$tmp.ssalvn.s" "$tmp.ssalvn.log" \
    "$tmp.ssastrength.s" "$tmp.ssastrength.log" \
    "$tmp.staticspec.s" "$tmp.staticspec.os.s" "$tmp.staticspec.free.s" \
    "$tmp.staticspec.generic.s" \
    "$tmp.staticspec-stack.s" \
    "$tmp.weak-first.s" "$tmp.weak-first.o" \
    "$tmp.weak-second.s" "$tmp.weak-second.o" \
    "$tmp.weak-start.s" "$tmp.weak-start.o" "$tmp.weak.elf"' 0 1 2 3 15

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

"$pcc" -O2 -S -o "$tmp.staticspec.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssaspecialize001.c"
test "$(grep -c '^__pcc_spec_.*:$' "$tmp.staticspec.s")" -eq 2
grep 'jal[[:space:]]*__pcc_spec_1_sum_stride' "$tmp.staticspec.s" >/dev/null
grep 'jal[[:space:]]*sum_stride' "$tmp.staticspec.s" >/dev/null
grep 'sum_stride_pointer' "$tmp.staticspec.s" >/dev/null
if [ "$float_abi" = hard ]; then
	awk '
/^[[:space:]]*[.]ent main$/ { inside = 1; next }
inside && /jal[[:space:]]*__pcc_spec_1_sum_stride/ { found = 1; exit }
inside && /^[[:space:]]*(li|move|addiu)[[:space:]]+\$a[23],/ { bad = 1 }
END { exit found && !bad ? 0 : 1 }
	' "$tmp.staticspec.s" || {
		echo "specialized register constants were still materialized" >&2
		exit 1
	}
fi
"$pcc" -Os -S -o "$tmp.staticspec.os.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssaspecialize001.c"
if grep '__pcc_spec_' "$tmp.staticspec.os.s" >/dev/null; then
	echo "-Os unexpectedly enabled static specialization" >&2
	exit 1
fi
"$pcc" -O2 -ffreestanding -S -o "$tmp.staticspec.free.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssaspecialize001.c"
if grep '__pcc_spec_' "$tmp.staticspec.free.s" >/dev/null; then
	echo "freestanding compilation unexpectedly enabled static specialization" >&2
	exit 1
fi

"$pcc" -march=mips3 -mtune=r4000 -O2 -S \
    -o "$tmp.staticspec.generic.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssaspecialize001.c"
awk '
/^[[:space:]]*[.]ent main$/ { inside = 1; next }
inside && /^[[:space:]]*(li|move|addiu)[[:space:]]+\$a2,/ { a2 = 1 }
inside && /^[[:space:]]*(li|move|addiu)[[:space:]]+\$a3,/ { a3 = 1 }
inside && /jal[[:space:]]*__pcc_spec_1_sum_stride/ { found = 1; exit }
END { exit found && a2 && a3 ? 0 : 1 }
' "$tmp.staticspec.generic.s" || {
	echo "generic MIPS3 unexpectedly elided specialized arguments" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
static int six_arg(int, int, int, int, int *, int);

int
static_stack_arg_probe(int *out)
{
	return six_arg(2, 3, 0, 1, out, 1);
}

static int
six_arg(int a, int b, int zero, int one, int *out, int tail)
{
	*out = a + b + zero + one + tail;
	return *out;
}
EOF
"$pcc" -O2 -S -o "$tmp.staticspec-stack.s" "$tmp.c"
grep 'jal[[:space:]]*__pcc_spec_.*_six_arg' \
    "$tmp.staticspec-stack.s" >/dev/null
if [ "$float_abi" = hard ]; then
	awk '
/^[[:space:]]*[.]ent static_stack_arg_probe$/ { inside = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*sw[[:space:]].*,16\(\$sp\)/ { slot16++ }
inside && /^[[:space:]]*sw[[:space:]].*,20\(\$sp\)/ { slot20++ }
inside && /jal[[:space:]]*__pcc_spec_.*_six_arg/ { found = 1; exit }
END { exit found && slot16 == 1 && slot20 == 0 ? 0 : 1 }
	' "$tmp.staticspec-stack.s" || {
		echo "specialized stack constant was not elided safely" >&2
		exit 1
	}
	awk '
/^[[:space:]]*[.]ent __pcc_spec_.*_six_arg$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*lw[[:space:]].*,32\(\$sp\)/ { pointer_loads++ }
inside && /,36\(\$sp\)/ { dead_tail_slot = 1 }
END { exit seen && pointer_loads == 1 && !dead_tail_slot ? 0 : 1 }
	' "$tmp.staticspec-stack.s" || {
		echo "specialized stack pointer was repeatedly loaded" >&2
		exit 1
	}
fi

cat > "$tmp.c" <<'EOF'
extern int weak_data __attribute__((weak));
void weak_func(void) __attribute__((weak));
int weak_probe(void) { return (&weak_data != 0) || (weak_func != 0); }
EOF

"$pcc" -S -o "$tmp.s" "$tmp.c"
grep '[.]weak.*weak_data' "$tmp.s" >/dev/null
grep '[.]weak.*weak_func' "$tmp.s" >/dev/null
"$pcc" -c -o "$tmp.o" "$tmp.c"
test -s "$tmp.o"

if [ -n "$ldscript" ] && [ -x "$tool_src_dir/nm" ]; then
	cat > "$tmp.weak-first.s" <<'EOF'
	.text
	.weak weak_choice
	.globl weak_choice
	.globl weak_first_marker
	.ent weak_choice
weak_choice:
weak_first_marker:
	jr $ra
	li $v0,1
EOF
	cat > "$tmp.weak-second.s" <<'EOF'
	.text
	.weak weak_choice
	.globl weak_choice
	.globl weak_second_marker
	.ent weak_choice
weak_choice:
weak_second_marker:
	jr $ra
	li $v0,2
EOF
	cat > "$tmp.weak-start.s" <<'EOF'
	.text
	.globl _start
	.ent _start
_start:
	jal weak_choice
	nop
EOF
	"$as" --elf "$elf_endian" -march="$cpu" "$tmp.weak-first.s" \
	    -o "$tmp.weak-first.o"
	"$as" --elf "$elf_endian" -march="$cpu" "$tmp.weak-second.s" \
	    -o "$tmp.weak-second.o"
	"$as" --elf "$elf_endian" -march="$cpu" "$tmp.weak-start.s" \
	    -o "$tmp.weak-start.o"
	"$ld" --elf "$elf_endian" -T "$ldscript" -e _start \
	    -o "$tmp.weak.elf" "$tmp.weak-first.o" "$tmp.weak-second.o" \
	    "$tmp.weak-start.o"
	weak_choice_addr=$("$tool_src_dir/nm" "$tmp.weak.elf" |
	    awk '$3 == "weak_choice" { print $1 }')
	weak_first_addr=$("$tool_src_dir/nm" "$tmp.weak.elf" |
	    awk '$3 == "weak_first_marker" { print $1 }')
	if [ -z "$weak_choice_addr" ] ||
	    [ "$weak_choice_addr" != "$weak_first_addr" ]; then
		echo "ELF linker did not preserve the first weak definition" >&2
		exit 1
	fi
fi

cat > "$tmp.c" <<'EOF'
struct sd {
	char c;
	double d;
};
typedef char check_sd_size[(sizeof(struct sd) == EXPECT_SIZE) ? 1 : -1];
EOF

"$pcc" -march=vr4300 -DEXPECT_SIZE=16 -c -o "$tmp.o" "$tmp.c"
"$pcc" -march=mips32r2 -DEXPECT_SIZE=12 -c -o "$tmp.o" "$tmp.c"

cat > "$tmp.c" <<'EOF'
int
mul_probe(int a, int b)
{
	return a * b;
}
EOF

"$pcc" -march=vr4300 -S -o "$tmp.s" "$tmp.c"
if grep '^[[:space:]]*mul[[:space:]]' "$tmp.s" >/dev/null; then
	echo "VR4300 emitted MIPS32r2 mul" >&2
	exit 1
fi
grep '^[[:space:]]*mult[[:space:]]' "$tmp.s" >/dev/null
awk '
/^[[:space:]]*mult[[:space:]]/ { state = 1; next }
state == 1 && /^[[:space:]]*mflo[[:space:]]/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "VR4300 retained software padding before interlocked mflo" >&2
	exit 1
}
"$pcc" -march=mips3 -mtune=r4000 -S -o "$tmp.s" "$tmp.c"
grep '^[[:space:]]*mult[[:space:]]' "$tmp.s" >/dev/null
awk '
/^[[:space:]]*mult[[:space:]]/ { state = 1; next }
state == 1 && /^[[:space:]]*nop([[:space:]]|$)/ { state = 2; next }
state == 2 && /^[[:space:]]*nop([[:space:]]|$)/ { state = 3; next }
state == 3 && /^[[:space:]]*mflo[[:space:]]/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 lost conservative multiply padding" >&2
	exit 1
}
"$pcc" -march=mips32r2 -S -o "$tmp.s" "$tmp.c"
grep '^[[:space:]]*mul[[:space:]]' "$tmp.s" >/dev/null
"$pcc" -mips32r2 -S -o "$tmp.s" "$tmp.c"
grep '^[[:space:]]*mul[[:space:]]' "$tmp.s" >/dev/null

cat > "$tmp.c" <<'EOF'
int
load_schedule_probe(int *array, int stride, int row, int column)
{
	int index = stride * row + column;

	return array[index] + index;
}

int
param_address_probe(int value)
{
	int *pointer = &value;

	*pointer += 3;
	return value;
}

int
param_assign_probe(int value, int replace)
{
	if (replace)
		value = 17;
	return value;
}
EOF

for param_cpu in mips32r2 vr4300; do
	"$pcc" -march="$param_cpu" -O2 -fno-omit-frame-pointer -S \
	    -o "$tmp.s" "$tmp.c"
	awk '
/^[[:space:]]*[.]ent load_schedule_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*sll[[:space:]]/ { shift = 1 }
inside && /,(16|20|24|28)\(\$fp\)/ { frame_arg = 1 }
END { exit seen && shift && !frame_arg ? 0 : 1 }
' "$tmp.s" || {
		echo "$param_cpu did not promote scalar register parameters" >&2
		exit 1
	}
	awk '
/^[[:space:]]*[.]ent param_address_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*sw[[:space:]]+\$a0,-[0-9]+\(\$fp\)/ { store = 1 }
inside && /^[[:space:]]*lw[[:space:]].*-[0-9]+\(\$fp\)/ { load = 1 }
END { exit seen && store && load ? 0 : 1 }
' "$tmp.s" || {
		echo "$param_cpu did not materialize an address-taken parameter" >&2
		exit 1
	}
	awk '
/^[[:space:]]*[.]ent param_assign_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /,(16|20)\(\$fp\)/ { frame_arg = 1 }
inside && /^[[:space:]]*li[[:space:]].*,17([[:space:]]|$)/ { assign = 1 }
END { exit seen && assign && !frame_arg ? 0 : 1 }
' "$tmp.s" || {
		echo "$param_cpu did not keep an assigned parameter in a TEMP" >&2
		exit 1
	}
done

"$pcc" -march=mips3 -mtune=r4000 -O2 -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*[.]ent load_schedule_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*sw[[:space:]]+\$a0,16\(\$fp\)/ { store = 1 }
inside && /^[[:space:]]*lw[[:space:]].*,16\(\$fp\)/ { load = 1 }
END { exit seen && store && load ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 unexpectedly promoted scalar register parameters" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
int
branch_schedule_probe(int unused1, int unused2, int unused3, int unused4,
    int *value)
{
	int result = 1;

	if (*value >= 0)
		result = *value + 2;
	return result;
}
EOF

"$pcc" -march=vr4300 -O2 -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*lw \$a0,0\(\$v1\)/ { state = 1; next }
state == 1 && /^[[:space:]]*bltz \$a0,/ { state = 2; next }
state == 2 && /^[[:space:]]*addiu \$v0,\$zero,1/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "VR4300 did not fill branch delay across independent load" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
void
fpu_latency_schedule_probe(void)
{
	asm("mul.d $f4,$f2,$f0\n\t"
	    "add.d $f6,$f6,$f4\n\t"
	    "lw $a0,20($fp)\n\t"
	    "nop\n\t"
	    "addiu $v1,$v0,1");
}
EOF

for schedule_cpu in vr4300 mips32r2; do
	"$pcc" -march="$schedule_cpu" -mhard-float -O2 \
	    -fno-omit-frame-pointer -S -o "$tmp.s" "$tmp.c"
	awk '
/^[[:space:]]*mul\.d[[:space:]]+\$f4,\$f2,\$f0/ { state = 1; next }
state == 1 && /^[[:space:]]*lw[[:space:]]+\$a0,20\(\$fp\)/ {
	state = 2
	next
}
state == 2 && /^[[:space:]]*add\.d[[:space:]]+\$f6,\$f6,\$f4/ {
	state = 3
	next
}
state == 3 && /^[[:space:]]*addiu[[:space:]]+\$v1,\$v0,1/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu did not separate dependent FPU operations" >&2
		exit 1
	}
done

"$pcc" -march=mips3 -mtune=r4000 -mhard-float -O2 \
    -fno-omit-frame-pointer -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*mul\.d[[:space:]]+\$f4,\$f2,\$f0/ { state = 1; next }
state == 1 && /^[[:space:]]*add\.d[[:space:]]+\$f6,\$f6,\$f4/ {
	found = 1
}
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 unexpectedly received VR4300 FPU scheduling" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
double
fpu_param_probe(int count, double scale, double *values)
{
	double total = 0.0;
	int i;

	for (i = 0; i < count; i++) {
		total += scale * values[i];
		values[i] = scale * values[i];
	}
	return total;
}

void
fpu_load_move_schedule_probe(void)
{
	asm("l.d $f4,0($a0)\n\t"
	    "nop\n\t"
	    "mov.d $f2,$f0\n\t"
	    "mul.d $f6,$f2,$f4");
}
EOF

for schedule_cpu in vr4300 mips32r2; do
	"$pcc" -march="$schedule_cpu" -mhard-float -O2 \
	    -fno-omit-frame-pointer -S -o "$tmp.s" "$tmp.c"
	awk '
/^[[:space:]]*[.]ent fpu_param_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*(l[.]d|ldc1)[[:space:]].*24\(\$fp\)/ { loads++ }
inside && /^[[:space:]]*mov[.]d[[:space:]]/ { moves++ }
END { exit seen && loads == 1 && moves >= 2 ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu did not keep a GPR-passed double in a TEMP" >&2
		exit 1
	}
	awk '
/^[[:space:]]*[.]ent fpu_load_move_schedule_probe$/ {
	inside = 1; seen = 1; next
}
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*mov[.]d[[:space:]]+\$f2,\$f0/ {
	state = 1; next
}
inside && state == 1 && /^[[:space:]]*l[.]d[[:space:]]+\$f4,0\(\$a0\)/ {
	state = 2; next
}
inside && state == 2 && /^[[:space:]]*mul[.]d[[:space:]]+\$f6,\$f2,\$f4/ {
	found = 1
}
END { exit seen && found ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu did not fill the FPU load gap with mov.d" >&2
		exit 1
	}
done

"$pcc" -march=mips3 -mtune=r4000 -mhard-float -O2 \
    -fno-omit-frame-pointer -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*[.]ent fpu_param_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*(l[.]d|ldc1)[[:space:]].*24\(\$fp\)/ { loads++ }
inside && /^[[:space:]]*mov[.]d[[:space:]]/ { moves++ }
END { exit seen && loads == 2 && moves == 0 ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 unexpectedly promoted a GPR-passed double" >&2
	exit 1
}
awk '
/^[[:space:]]*[.]ent fpu_load_move_schedule_probe$/ {
	inside = 1; seen = 1; next
}
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*l[.]d[[:space:]]+\$f4,0\(\$a0\)/ {
	state = 1; next
}
inside && state == 1 && /^[[:space:]]*nop([[:space:]]|$)/ {
	state = 2; next
}
inside && state == 2 && /^[[:space:]]*mov[.]d[[:space:]]+\$f2,\$f0/ {
	state = 3; next
}
inside && state == 3 && /^[[:space:]]*mul[.]d[[:space:]]+\$f6,\$f2,\$f4/ {
	found = 1
}
END { exit seen && found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 unexpectedly filled the FPU load/move gap" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
void
fpu_conversion_schedule_probe(void)
{
	asm("mtc1 $v0,$f0\n\t"
	    "nop\n\t"
	    "cvt.d.w $f0,$f0\n\t"
	    "l.d $f2,0($a0)\n\t"
	    "nop\n\t"
	    "add.d $f4,$f0,$f2\n\t"
	    "addiu $v1,$v1,1");
}

void
fpu_mtc1_interlock_probe(void)
{
	asm("mtc1 $v0,$f0\n\t"
	    "nop\n\t"
	    "cvt.d.w $f0,$f0");
}

void
fpu_conversion_hilo_schedule_probe(void)
{
	asm("mult $a0,$a1\n\t"
	    "nop\n\t"
	    "nop\n\t"
	    "mflo $v0\n\t"
	    "mtc1 $a2,$f0\n\t"
	    "nop\n\t"
	    "cvt.d.w $f0,$f0\n\t"
	    "l.d $f2,0($a3)\n\t"
	    "nop\n\t"
	    "add.d $f4,$f0,$f2");
}
EOF

for schedule_cpu in vr4300 mips32r2; do
	"$pcc" -march="$schedule_cpu" -mhard-float -O2 -S -o "$tmp.s" \
	    "$tmp.c"
	awk '
/^[[:space:]]*[.]ent fpu_mtc1_interlock_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*mtc1[[:space:]]+\$v0,\$f0/ { state = 1; next }
inside && state == 1 && /^[[:space:]]*cvt[.]d[.]w[[:space:]]+\$f0,\$f0/ {
	found = 1
}
END { exit seen && found ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu retained the interlocked mtc1 conversion nop" >&2
		exit 1
	}
	awk '
/^[[:space:]]*mtc1[[:space:]]+\$v0,\$f0/ { state = 1; next }
state == 1 && /^[[:space:]]*l\.d[[:space:]]+\$f2,0\(\$a0\)/ {
	state = 2
	next
}
state == 2 && /^[[:space:]]*cvt\.d\.w[[:space:]]+\$f0,\$f0/ {
	state = 3
	next
}
state == 3 && /^[[:space:]]*add\.d[[:space:]]+\$f4,\$f0,\$f2/ {
	state = 4
	next
}
state == 4 && /^[[:space:]]*addiu[[:space:]]+\$v1,\$v1,1/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu did not fill FPU transfer/conversion gaps" >&2
		exit 1
	}
	awk '
/^[[:space:]]*mult[[:space:]]+\$a0,\$a1/ { state = 1; next }
state == 1 && /^[[:space:]]*mtc1[[:space:]]+\$a2,\$f0/ {
	state = 2
	next
}
state == 2 && /^[[:space:]]*nop[[:space:]]*$/ { state = 3; next }
state == 3 && /^[[:space:]]*mflo[[:space:]]+\$v0/ { state = 4; next }
state == 4 && /^[[:space:]]*cvt\.d\.w[[:space:]]+\$f0,\$f0/ {
	state = 5
	next
}
state == 5 && /^[[:space:]]*l\.d[[:space:]]+\$f2,0\(\$a3\)/ {
	state = 6
	next
}
state == 6 && /^[[:space:]]*add\.d[[:space:]]+\$f4,\$f0,\$f2/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu regressed combined HI/LO and FPU gaps" >&2
		exit 1
	}
done

"$pcc" -march=mips3 -mtune=r4000 -mhard-float -O2 -S -o "$tmp.s" \
    "$tmp.c"
awk '
/^[[:space:]]*[.]ent fpu_mtc1_interlock_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*mtc1[[:space:]]+\$v0,\$f0/ { state = 1; next }
inside && state == 1 && /^[[:space:]]*nop[[:space:]]*$/ { state = 2; next }
inside && state == 2 && /^[[:space:]]*cvt[.]d[.]w[[:space:]]+\$f0,\$f0/ {
	found = 1
}
END { exit seen && found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 lost the conservative mtc1 conversion nop" >&2
	exit 1
}
awk '
/^[[:space:]]*mtc1[[:space:]]+\$v0,\$f0/ { state = 1; next }
state == 1 && /^[[:space:]]*l\.d[[:space:]]+\$f2,0\(\$a0\)/ {
	state = 2
	next
}
state == 2 && /^[[:space:]]*cvt\.d\.w[[:space:]]+\$f0,\$f0/ {
	state = 3
	next
}
state == 3 && /^[[:space:]]*add\.d[[:space:]]+\$f4,\$f0,\$f2/ {
	state = 4
	next
}
state == 4 && /^[[:space:]]*addiu[[:space:]]+\$v1,\$v1,1/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 lost FPU conversion scheduling" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
extern void fpu_reload_consume(int, double *, double);
extern void fpu_reload_mutate(double *);

void
fpu_expr_call_probe(double *values, int index)
{
	double value = -values[index];

	fpu_reload_consume(index, values, value);
}

double
fpu_expr_return_probe(double value)
{
	double result = -value;

	return result;
}

double
fpu_expr_volatile_probe(double value)
{
	volatile double result = -value;

	return result;
}

void
fpu_expr_nested_call_probe(double *values)
{
	double value = -values[0];

	fpu_reload_mutate(&value);
	fpu_reload_consume(0, values, value);
}
EOF

for reload_cpu in vr4300 mips32r2; do
	"$pcc" -march="$reload_cpu" -mhard-float -O2 \
	    -fno-omit-frame-pointer -S -o "$tmp.s" "$tmp.c"
	for folded in fpu_expr_call_probe fpu_expr_return_probe; do
		awk -v fn="$folded" '
$0 ~ "^[[:space:]]*[.]ent " fn "$" { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*neg[.]d[[:space:]]/ { neg = 1 }
inside && /^[[:space:]]*(l[.]d|ldc1)[[:space:]].*\(\$fp\)/ {
	frame_load = 1
}
END { exit seen && neg && !frame_load ? 0 : 1 }
' "$tmp.s" || {
			echo "$reload_cpu did not fold $folded stack reload" >&2
			exit 1
		}
	done
	awk '
/^[[:space:]]*[.]ent fpu_expr_volatile_probe$/ {
	inside = 1; seen = 1; next
}
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*(s[.]d|sdc1)[[:space:]].*\(\$fp\)/ { store = 1 }
inside && /^[[:space:]]*(l[.]d|ldc1)[[:space:]].*\(\$fp\)/ { load = 1 }
END { exit seen && store && load ? 0 : 1 }
' "$tmp.s" || {
		echo "$reload_cpu folded a volatile FPU stack reload" >&2
		exit 1
	}
	awk '
/^[[:space:]]*[.]ent fpu_expr_nested_call_probe$/ {
	inside = 1; seen = 1; next
}
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*jal[[:space:]]+fpu_reload_mutate/ {
	mutate = 1; next
}
inside && mutate && /^[[:space:]]*(l[.]d|ldc1)[[:space:]].*\(\$fp\)/ {
	reload = 1
}
END { exit seen && mutate && reload ? 0 : 1 }
' "$tmp.s" || {
		echo "$reload_cpu moved an FPU reload across a nested call" >&2
		exit 1
	}
done

cat > "$tmp.c" <<'EOF'
void
hilo_gap_schedule_probe(void)
{
	asm("mult $v1,$v0\n\t"
	    "nop\n\t"
	    "nop\n\t"
	    "mflo $v1\n\t"
	    "move $v0,$zero");
}

void
hilo_gap_dependency_probe(void)
{
	asm("mult $a0,$a1\n\t"
	    "nop\n\t"
	    "nop\n\t"
	    "mflo $v0\n\t"
	    "addiu $v1,$v0,1");
}
EOF

for schedule_cpu in vr4300 mips32r2; do
	"$pcc" -march="$schedule_cpu" -O2 -S -o "$tmp.s" "$tmp.c"
	awk '
/^[[:space:]]*mult[[:space:]]+\$v1,\$v0/ { positive = 1; next }
positive == 1 && /^[[:space:]]*move[[:space:]]+\$v0,\$zero/ {
	positive = 2
	next
}
positive == 2 && /^[[:space:]]*nop([[:space:]]|$)/ {
	positive = 3
	next
}
positive == 3 && /^[[:space:]]*mflo[[:space:]]+\$v1/ { positive_ok = 1 }
/^[[:space:]]*mult[[:space:]]+\$a0,\$a1/ { negative = 1; next }
negative == 1 && /^[[:space:]]*nop([[:space:]]|$)/ {
	negative = 2
	next
}
negative == 2 && /^[[:space:]]*nop([[:space:]]|$)/ {
	negative = 3
	next
}
negative == 3 && /^[[:space:]]*mflo[[:space:]]+\$v0/ {
	negative = 4
	next
}
negative == 4 && /^[[:space:]]*addiu[[:space:]]+\$v1,\$v0,1/ {
	negative_ok = 1
}
END { exit positive_ok && negative_ok ? 0 : 1 }
' "$tmp.s" || {
		echo "$schedule_cpu failed HI/LO gap dependency scheduling" >&2
		exit 1
	}
done

"$pcc" -march=mips3 -mtune=r4000 -O2 -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*mult[[:space:]]+\$v1,\$v0/ { state = 1; next }
state == 1 && /^[[:space:]]*nop([[:space:]]|$)/ { state = 2; next }
state == 2 && /^[[:space:]]*nop([[:space:]]|$)/ { state = 3; next }
state == 3 && /^[[:space:]]*mflo[[:space:]]+\$v1/ { state = 4; next }
state == 4 && /^[[:space:]]*move[[:space:]]+\$v0,\$zero/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "generic MIPS3 unexpectedly received HI/LO gap scheduling" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
#include <stdarg.h>

extern int frame_reg_callee(int, int);
extern int frame_pad_callee(int, int, int, int, int);
extern int frame_stack_callee(int, int, int, int, int, int);
extern int frame_wide_callee(int, int, int, int, long long, int);
extern int frame_variadic_callee(int, ...);
extern int frame_address_callee(int *);
extern void *alloca(unsigned);

struct frame_words {
	int a, b, c;
};

extern int frame_struct_callee(int, int, int, int, struct frame_words);

int
frame_reg_probe(int *value, int addend)
{
	int saved = *value;

	return frame_reg_callee(saved, addend) + saved;
}

int
frame_stack_probe(int value)
{
	return frame_stack_callee(value, 2, 3, 4, 5, 6);
}

int
frame_pad_probe(int value)
{
	return frame_pad_callee(value, 2, 3, 4, 5);
}

int
frame_wide_probe(int value)
{
	return frame_wide_callee(value, 2, 3, 4, 0x1122334455667788LL, 6);
}

int
frame_variadic_call_probe(int value)
{
	return frame_variadic_callee(value, 2, 3, 4, 5, 6);
}

int
frame_struct_probe(int value)
{
	struct frame_words words = { value, 2, 3 };

	return frame_struct_callee(value, 2, 3, 4, words);
}

int
frame_nested_probe(int value)
{
	return frame_stack_callee(value, 2, 3, 4,
	    frame_stack_callee(value, 6, 7, 8, 9, 10), 11);
}

int
frame_address_probe(int value)
{
	frame_address_callee(&value);
	return value;
}

int
frame_alloca_probe(unsigned size)
{
	char *value = alloca(size);

	value[0] = (char)size;
	return value[0];
}

int
frame_varargs_probe(int count, ...)
{
	va_list ap;
	int value;

	va_start(ap, count);
	value = va_arg(ap, int);
	va_end(ap);
	return count + value;
}
EOF

for frame_cpu in vr4300 mips32r2; do
	"$pcc" -march="$frame_cpu" -O2 -fomit-frame-pointer -S \
	    -o "$tmp.s" "$tmp.c"
	"$pcc" -march="$frame_cpu" -O2 -fomit-frame-pointer -c \
	    -o "$tmp.o" "$tmp.c"
	"$pcc" -march="$frame_cpu" -O2 -S -o "$tmp.default.s" "$tmp.c"
	cmp -s "$tmp.s" "$tmp.default.s"
	"$pcc" -march="$frame_cpu" -O2 -fno-omit-frame-pointer -S \
	    -o "$tmp.noomit.s" "$tmp.c"
	awk '
/^[[:space:]]*[.]ent frame_reg_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /[.]frame \$fp/ { fp = 1 }
END { exit seen && fp ? 0 : 1 }
' "$tmp.noomit.s" || {
		echo "$frame_cpu ignored -fno-omit-frame-pointer" >&2
		exit 1
	}

	awk '
/^[[:space:]]*[.]ent frame_reg_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
inside && /^[[:space:]]*jal frame_reg_callee/ { call = 1; next }
call && /^[[:space:]]*subu \$sp,\$sp,16/ { dynamic = 1 }
call { call = 0 }
inside && /^[[:space:]]*sw \$s[0-7],(0|[1-9]|1[0-5])\(\$sp\)/ {
	low_save = 1
}
END { exit seen && !fp && !dynamic && !low_save ? 0 : 1 }
' "$tmp.s" || {
		echo "$frame_cpu did not use a disjoint fixed call area" >&2
		exit 1
	}

	awk '
/^[[:space:]]*[.]ent frame_pad_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
inside && /^[[:space:]]*sw .*,16\(\$sp\)/ { arg16 = 1 }
inside && /^[[:space:]]*sw .*,20\(\$sp\)/ { arg20 = 1 }
END { exit seen && !fp && arg16 && !arg20 ? 0 : 1 }
' "$tmp.s" || {
		echo "$frame_cpu misplaced a padded stack argument" >&2
		exit 1
	}

	awk '
/^[[:space:]]*[.]ent frame_wide_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /^[[:space:]]*sw .*,16\(\$sp\)/ { arg16 = 1 }
inside && /^[[:space:]]*sw .*,20\(\$sp\)/ { arg20 = 1 }
inside && /^[[:space:]]*sw .*,24\(\$sp\)/ { arg24 = 1 }
END { exit seen && arg16 && arg20 && arg24 ? 0 : 1 }
' "$tmp.s" || {
		echo "$frame_cpu broke aligned 64-bit stack slots" >&2
		exit 1
	}

	awk '
/^[[:space:]]*[.]ent frame_stack_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
inside && /^[[:space:]]*(addiu|subu) \$sp,\$sp,-4/ { push = 1 }
inside && /^[[:space:]]*li \$v0,6([[:space:]]|$)/ { value6 = 1; next }
inside && value6 && /^[[:space:]]*sw \$v0,20\(\$sp\)/ {
	arg6 = 1; value6 = 0; next
}
inside && /^[[:space:]]*li \$v0,5([[:space:]]|$)/ { value5 = 1; next }
inside && value5 && /^[[:space:]]*sw \$v0,16\(\$sp\)/ {
	arg5 = 1; value5 = 0; next
}
END { exit seen && !fp && !push && arg5 && arg6 ? 0 : 1 }
' "$tmp.s" || {
		echo "$frame_cpu did not fix ordinary stack arguments" >&2
		exit 1
	}

	for fixed_stack in frame_wide_probe frame_variadic_call_probe \
	    frame_nested_probe; do
		awk -v fn="$fixed_stack" '
$0 ~ "^[[:space:]]*[.]ent " fn "$" { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
inside && /^[[:space:]]*(addiu|subu) \$sp,\$sp,-(4|8)/ { push = 1 }
END { exit seen && !fp && !push ? 0 : 1 }
' "$tmp.s" || {
			echo "$frame_cpu did not fix stack arguments in $fixed_stack" >&2
			exit 1
		}
	done

	for protected in frame_struct_probe frame_address_probe frame_alloca_probe \
	    frame_varargs_probe; do
		awk -v fn="$protected" '
$0 ~ "^[[:space:]]*[.]ent " fn "$" { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
END { exit seen && fp ? 0 : 1 }
' "$tmp.s" || {
			echo "$frame_cpu omitted required frame in $protected" >&2
			exit 1
		}
	done
done

if "$pcc" -march=mips32 -S -o "$tmp.s" "$tmp.c" >"$tmp.err" 2>&1; then
	echo "unsupported MIPS32r1 profile was accepted" >&2
	exit 1
fi
grep 'march=mips32 is unsupported' "$tmp.err" >/dev/null
if "$pcc" -march=mips32r2 -mtune=vr4300 -S -o "$tmp.s" "$tmp.c" \
    >"$tmp.err" 2>&1; then
	echo "incompatible MIPS32r2/VR4300 tuning was accepted" >&2
	exit 1
fi
grep 'MIPS III tuning requires -march=mips3' "$tmp.err" >/dev/null

for tune in generic r4000; do
	"$pcc" -march=mips3 -mtune="$tune" -S -o "$tmp.s" "$tmp.c"
done
for tune in generic 24kc 34kc 74kc jz4780; do
	"$pcc" -march=mips32r2 -mtune="$tune" -S -o "$tmp.s" "$tmp.c"
done

cat > "$tmp.c" <<'EOF'
void
erratum_probe(void)
{
	asm("mul.s $f0,$f2,$f4\n\tmult $t0,$t1");
}
EOF

"$pcc" -march=mips3 -mtune=vr4300 -mfix4300 \
    -S -o "$tmp.s" "$tmp.c"
grep 'VR4300 fp multiply erratum' "$tmp.s" >/dev/null
"$pcc" -march=mips3 -mtune=vr4300 -mno-fix4300 \
    -S -o "$tmp.s" "$tmp.c"
if grep 'VR4300 fp multiply erratum' "$tmp.s" >/dev/null; then
	echo "-mno-fix4300 still inserted the erratum nop" >&2
	exit 1
fi
"$pcc" -march=mips3 -mtune=r4000 -mfix4300 \
    -S -o "$tmp.s" "$tmp.c"
if grep 'VR4300 fp multiply erratum' "$tmp.s" >/dev/null; then
	echo "R4000 tuning received the VR4300 erratum nop" >&2
	exit 1
fi
"$pcc" -march=mips32r2 -mfix4300 -S -o "$tmp.s" "$tmp.c"
if grep 'VR4300 fp multiply erratum' "$tmp.s" >/dev/null; then
	echo "MIPS32r2 received the VR4300 erratum nop" >&2
	exit 1
fi

"$pcc" -march=mips32r2 -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips 32$' "$tmp.macros" >/dev/null
grep '^#define __mips32r2' "$tmp.macros" >/dev/null
"$pcc" -march=vr4300 -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips 3$' "$tmp.macros" >/dev/null
grep '^#define __vr4300__' "$tmp.macros" >/dev/null
"$pcc" -march=mips3 -mtune=r4000 -E -dM "$tmp.c" > "$tmp.macros"
if grep '^#define __vr4300__' "$tmp.macros" >/dev/null; then
	echo "R4000 tuning defined __vr4300__" >&2
	exit 1
fi
"$pcc" -mhard-float -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips_hard_float' "$tmp.macros" >/dev/null
if grep '^#define __mips_soft_float' "$tmp.macros" >/dev/null; then
	echo "hard-float preprocessing defined soft-float" >&2
	exit 1
fi
"$pcc" -msoft-float -E -dM "$tmp.c" > "$tmp.macros"
grep '^#define __mips_soft_float' "$tmp.macros" >/dev/null
if grep '^#define __mips_hard_float' "$tmp.macros" >/dev/null; then
	echo "soft-float preprocessing defined hard-float" >&2
	exit 1
fi
"$pcc" -E -dM "$tmp.c" > "$tmp.macros"
if [ "$float_abi" = soft ]; then
	grep '^#define __mips_soft_float' "$tmp.macros" >/dev/null
else
	grep '^#define __mips_hard_float' "$tmp.macros" >/dev/null
fi
if [ "$endian" = big ]; then
	grep '^#define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__$' "$tmp.macros" >/dev/null
	grep '^#define __MIPSEB__' "$tmp.macros" >/dev/null
	"$pcc" -mbig-endian -E -dM "$tmp.c" > "$tmp.macros"
	grep '^#define __MIPSEB__' "$tmp.macros" >/dev/null
else
	grep '^#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__$' "$tmp.macros" >/dev/null
	grep '^#define __MIPSEL__' "$tmp.macros" >/dev/null
	"$pcc" -mlittle-endian -E -dM "$tmp.c" > "$tmp.macros"
	grep '^#define __MIPSEL__' "$tmp.macros" >/dev/null
fi

cat > "$tmp.c" <<'EOF'
extern double stats_call(double);

double
stats_probe(int n, double *a)
{
	double s0 = 1.0, s1 = 2.0, s2 = 3.0, s3 = 4.0;
	int i, j;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			s0 += a[i] * a[j];
			s1 += a[i] + a[j];
			s2 += s0 - s1;
			s3 += s2 * a[j];
		}
	}
	return stats_call(s0 + s1 + s2 + s3);
}
EOF

"$pcc" -O2 -fomit-frame-pointer -S -o "$tmp.normal.s" "$tmp.c" \
    2>"$tmp.stats.off"
test ! -s "$tmp.stats.off"
"$pcc" -O2 -fomit-frame-pointer -fopt-stats -S \
    -o "$tmp.stats.s" "$tmp.c" 2>"$tmp.stats.log"
cmp -s "$tmp.normal.s" "$tmp.stats.s"
grep '^PCC_OPTSTATS kind=function function=stats_probe ' \
    "$tmp.stats.log" >/dev/null
grep '^PCC_OPTSTATS kind=summary function=[*] ' "$tmp.stats.log" >/dev/null
for key in basic_blocks cfg_edges temps max_live_temps \
    interference_edges coalesce_attempts coalesce_successes \
    coalesce_rejected spill_candidates selected_spills reloads \
    spill_stores rematerialized gpr_pressure fpr_pressure frame_bytes \
    spill_area_bytes caller_saved_used callee_saved_used calls \
    max_loop_depth functions; do
	grep " $key=" "$tmp.stats.log" >/dev/null
done
"$pcc" -O2 -fomit-frame-pointer -fopt-stats -S \
    -o "$tmp.stats2.s" "$tmp.c" 2>"$tmp.stats2.log"
cmp -s "$tmp.stats.s" "$tmp.stats2.s"
cmp -s "$tmp.stats.log" "$tmp.stats2.log"
"$pcc" -O2 -fomit-frame-pointer -Wc,-xssa -S \
    -o "$tmp.ssa.s" "$tmp.c" 2>"$tmp.ssa.log"
test ! -s "$tmp.ssa.log"

"$pcc" -O2 -fomit-frame-pointer -Wc,-xssa -S \
    -o "$tmp.ssalvn.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssalvn001.c" \
    2>"$tmp.ssalvn.log"
test ! -s "$tmp.ssalvn.log"
repeated_scale_mults=$(awk '
    /^repeated_scale:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 ~ /^(mul|mult|multu|dmult|dmultu)$/ { count++ }
    END { print count + 0 }
' "$tmp.ssalvn.s")
changed_scale_mults=$(awk '
    /^changed_scale:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 ~ /^(mul|mult|multu|dmult|dmultu)$/ { count++ }
    END { print count + 0 }
' "$tmp.ssalvn.s")
test "$repeated_scale_mults" -eq 1
test "$changed_scale_mults" -eq 2
repeated_shift_slls=$(awk '
    /^repeated_shift:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 == "sll" { count++ }
    END { print count + 0 }
' "$tmp.ssalvn.s")
changed_shift_slls=$(awk '
    /^changed_shift:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 == "sll" { count++ }
    END { print count + 0 }
' "$tmp.ssalvn.s")
if [ "$float_abi" = hard ]; then
	test "$repeated_shift_slls" -eq 1
else
	test "$repeated_shift_slls" -eq 2
fi
test "$changed_shift_slls" -eq 2
"$pcc" -march=mips3 -mtune=r4000 -mhard-float -O2 \
    -fomit-frame-pointer -Wc,-xssa -S -o "$tmp.ssalvn.mips3.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssalvn001.c" \
    2>"$tmp.ssalvn.mips3.log"
test ! -s "$tmp.ssalvn.mips3.log"
generic_shift_slls=$(awk '
    /^repeated_shift:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 == "sll" { count++ }
    END { print count + 0 }
' "$tmp.ssalvn.mips3.s")
test "$generic_shift_slls" -eq 2

"$pcc" -O2 -fomit-frame-pointer -Wc,-xssa -S \
    -o "$tmp.ssastrength.s" \
    "$topsrc/src/dev/pcc/pcc-tests/regress/misc/ssastrength001.c" \
    2>"$tmp.ssastrength.log"
test ! -s "$tmp.ssastrength.log"
unit_step_mults=$(awk '
    /^unit_step:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 ~ /^(mul|mult|multu|dmult|dmultu)$/ { count++ }
    END { print count + 0 }
' "$tmp.ssastrength.s")
descending_mults=$(awk '
    /^descending:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 ~ /^(mul|mult|multu|dmult|dmultu)$/ { count++ }
    END { print count + 0 }
' "$tmp.ssastrength.s")
variable_step_mults=$(awk '
    /^variable_step:$/ { inside = 1; next }
    inside && $1 == ".ent" { inside = 0 }
    inside && $1 ~ /^(mul|mult|multu|dmult|dmultu)$/ { count++ }
    END { print count + 0 }
' "$tmp.ssastrength.s")
if [ "$cpu" = vr4300 ]; then
	test "$unit_step_mults" -eq 0
	test "$descending_mults" -eq 1
else
	test "$unit_step_mults" -eq 2
	test "$descending_mults" -eq 2
fi
test "$variable_step_mults" -eq 2

echo "smoke-host-portablecc: ok"
