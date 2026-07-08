#!/bin/sh
cd /var/tmp || exit 1
rm -f a.out pcc-smoke pcc-smoke.s pcc-smoke.o pcc-smoke.ro \
    pcc-fpu-smoke pcc-fpu-smoke.s pcc-fpu-smoke.o pcc-fpu-smoke.ro \
    pcc-weak-smoke pcc-weak-smoke.c pcc-weak-smoke.s \
    pcc-weak-smoke.o pcc-weak-smoke.ro

dump_asm_failure()
{
	file=$1
	echo "pcc-smoke: assembler input head: $file"
	sed -n '1,16p' "$file"
	echo "pcc-smoke: assembler input bytes: $file"
	od -c "$file" | sed -n '1,8p'
}

pcc -S -o pcc-smoke.s /root/pcc-smoke.c || exit 1
as -o pcc-smoke.o pcc-smoke.s || {
	dump_asm_failure pcc-smoke.s
	exit 1
}
ld -r -o pcc-smoke.ro pcc-smoke.o || exit 1
test -f pcc-smoke.ro || exit 1
echo "pcc/as/ld -r smoke ok"
pcc -o pcc-smoke /root/pcc-smoke.c || exit 1
test -f pcc-smoke || exit 1
./pcc-smoke || exit 1
echo "pcc link/run smoke ok"

cat > pcc-weak-smoke.c <<'EOF'
extern int weak_data __attribute__((weak));
void weak_func(void) __attribute__((weak));

int
main(void)
{
	return (&weak_data != 0) || (weak_func != 0);
}
EOF

pcc -S -o pcc-weak-smoke.s pcc-weak-smoke.c || exit 1
grep '[.]weak.*weak_data' pcc-weak-smoke.s >/dev/null || exit 1
grep '[.]weak.*weak_func' pcc-weak-smoke.s >/dev/null || exit 1
as -o pcc-weak-smoke.o pcc-weak-smoke.s || {
	dump_asm_failure pcc-weak-smoke.s
	exit 1
}
ld -r -o pcc-weak-smoke.ro pcc-weak-smoke.o || exit 1
pcc -o pcc-weak-smoke pcc-weak-smoke.c || exit 1
./pcc-weak-smoke || exit 1
echo "pcc weak smoke ok"

pcc -S -o pcc-fpu-smoke.s /root/pcc-fpu-smoke.c || exit 1
echo "pcc fpu compile smoke ok"
pcc -o pcc-fpu-smoke /root/pcc-fpu-smoke.c || exit 1
test -f pcc-fpu-smoke || exit 1
./pcc-fpu-smoke || exit 1
echo "pcc fpu link/run smoke ok"

rm -f a.out pcc-smoke pcc-smoke.s pcc-smoke.o pcc-smoke.ro \
    pcc-fpu-smoke pcc-fpu-smoke.s pcc-fpu-smoke.o pcc-fpu-smoke.ro \
    pcc-weak-smoke pcc-weak-smoke.c pcc-weak-smoke.s \
    pcc-weak-smoke.o pcc-weak-smoke.ro
