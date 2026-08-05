#!/bin/sh
#
# Shared MIPS target-side smoke test for the scripting/interpreter tools that
# are staged in both Malta and N64 root filesystems.
#

echo "lang-smoke diag v1"

tmp=/var/tmp/lang-smoke.$$
rm -rf "$tmp"
mkdir "$tmp" || exit 1
trap 'rm -rf "$tmp"' 0 1 2 3 15

echo "step 1: shell"
sh /root/sh-fail-smoke.sh || exit 1
sh /root/sh-comsubst-smoke.sh || exit 1
sh /root/sh-function-smoke.sh || exit 1

echo "step 2: awk"
echo '2 3' | awk '{ print $1 * $2 + 4 }' > "$tmp/awk.out" || exit 1
grep '^10$' "$tmp/awk.out" >/dev/null || {
	echo "lang-smoke: awk failed"
	cat "$tmp/awk.out"
	exit 1
}

echo "step 3: pdc"
pdc '6*7' > "$tmp/pdc.out" || exit 1
grep '42' "$tmp/pdc.out" >/dev/null || {
	echo "lang-smoke: pdc failed"
	cat "$tmp/pdc.out"
	exit 1
}

echo "step 4: forth"
cat > "$tmp/forth.fth" <<'EOF'
: square dup * ;
7 square .
cr
EOF
forth "$tmp/forth.fth" < /dev/null > "$tmp/forth.out" || exit 1
grep '49' "$tmp/forth.out" >/dev/null || {
	echo "lang-smoke: forth failed"
	cat "$tmp/forth.out"
	exit 1
}

echo "step 5: retroforth"
cat > "$tmp/retro.rx" <<'EOF'
6 7 * putn
bye
EOF
retroforth --with "$tmp/retro.rx" < /dev/null > "$tmp/retro.out" || exit 1
grep '42' "$tmp/retro.out" >/dev/null || {
	echo "lang-smoke: retroforth failed"
	cat "$tmp/retro.out"
	exit 1
}

echo "step 6: picoc"
cat > "$tmp/picoc.c" <<'EOF'
#include <stdio.h>

int f(int x)
{
	return x * x + 3;
}

int a[3];
struct PicocSmokeP {
	int x;
	int y;
};
struct PicocSmokeP p;

a[0] = 2;
a[1] = 5;
a[2] = a[0] * a[1];
p.x = 7;
p.y = f(a[2]);
printf("picoc ok %d %d\n", a[2], p.y);

void main() {}
EOF
picoc "$tmp/picoc.c" < /dev/null > "$tmp/picoc.out" || exit 1
grep '^picoc ok 10 103$' "$tmp/picoc.out" >/dev/null || {
	echo "lang-smoke: picoc failed"
	cat "$tmp/picoc.out"
	exit 1
}

echo "step 7: basic"
{
cat <<'EOF'
10 PRINT 6*7
20 EXIT
RUN
EOF
} | basic > "$tmp/basic.out" || exit 1
grep '42' "$tmp/basic.out" >/dev/null || {
	echo "lang-smoke: basic failed"
	cat "$tmp/basic.out"
	exit 1
}

echo "step 8: tcl"
cat > "$tmp/tcl.in" <<'EOF'
set a 2
set b 5
expr $a*$b+3
EOF
tcl < "$tmp/tcl.in" > "$tmp/tcl.out" || exit 1
grep '^13$' "$tmp/tcl.out" >/dev/null || {
	echo "lang-smoke: tcl failed"
	cat "$tmp/tcl.out"
	exit 1
}

echo "language smoke ok"
