#!/bin/sh
set -e

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
trap 'rm -f "$tmp.c" "$tmp.s" "$tmp.o" "$tmp.macros" "$tmp.err" \
    "$tmp.normal.s" "$tmp.stats.s" "$tmp.stats2.s" "$tmp.stats.off" \
    "$tmp.stats.log" "$tmp.stats2.log"' 0 1 2 3 15

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
extern int weak_data __attribute__((weak));
void weak_func(void) __attribute__((weak));
int weak_probe(void) { return (&weak_data != 0) || (weak_func != 0); }
EOF

"$pcc" -S -o "$tmp.s" "$tmp.c"
grep '[.]weak.*weak_data' "$tmp.s" >/dev/null
grep '[.]weak.*weak_func' "$tmp.s" >/dev/null
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
"$pcc" -mips3 -S -o "$tmp.s" "$tmp.c"
grep '^[[:space:]]*mult[[:space:]]' "$tmp.s" >/dev/null
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
EOF

"$pcc" -march=mips32r2 -O2 -S -o "$tmp.s" "$tmp.c"
awk '
/^[[:space:]]*lw \$a0,16\(\$fp\)/ { state = 1; next }
state == 1 && /^[[:space:]]*sll \$v0,\$v1,2/ { state = 2; next }
state == 2 && /^[[:space:]]*addu \$a0,\$a0,\$v0/ { found = 1 }
END { exit found ? 0 : 1 }
' "$tmp.s" || {
	echo "MIPS32r2 did not schedule independent shift after load" >&2
	exit 1
}

cat > "$tmp.c" <<'EOF'
int
branch_schedule_probe(int *value)
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
	"$pcc" -march="$schedule_cpu" -mhard-float -O2 -S -o "$tmp.s" \
	    "$tmp.c"
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

"$pcc" -march=mips3 -mtune=r4000 -mhard-float -O2 -S -o "$tmp.s" \
    "$tmp.c"
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
extern int frame_stack_callee(int, int, int, int, int, int);
extern int frame_address_callee(int *);
extern void *alloca(unsigned);

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
/^[[:space:]]*[.]ent frame_stack_probe$/ { inside = 1; seen = 1; next }
inside && /^[[:space:]]*[.]ent / { inside = 0 }
inside && /\$fp/ { fp = 1 }
inside && /^[[:space:]]*jal frame_stack_callee/ { call = 1; next }
call && /^[[:space:]]*subu \$sp,\$sp,16/ { dynamic = 1 }
call { call = 0 }
END { exit seen && fp && dynamic ? 0 : 1 }
' "$tmp.s" || {
		echo "$frame_cpu changed stack-argument frame lowering" >&2
		exit 1
	}

	for protected in frame_address_probe frame_alloca_probe \
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

echo "smoke-host-portablecc: ok"
