#!/bin/sh
#
# Shared MIPS target-side smoke test for secondary compiler backends staged in
# the rootfs.  This only proves that each backend can produce assembly accepted
# by the in-tree VR4300 assembler and linker as a relocatable object.
#

echo "secondary-cc-smoke diag v1"

tmp=/var/tmp/secondary-cc-smoke.$$
rm -rf "$tmp"
mkdir "$tmp" || exit 1
trap 'rm -rf "$tmp"' 0 1 2 3 15

cat > "$tmp/smallc.c" <<'EOF'
add(a, b)
int a;
int b;
{
	return a + b;
}

main()
{
	if (add(19, 23) == 42)
		return 0;
	return 1;
}
EOF

cat > "$tmp/ansi.c" <<'EOF'
int add(int a, int b)
{
	return a + b;
}

int main()
{
	if (add(19, 23) == 42)
		return 0;
	return 1;
}
EOF

echo "step 1: scc -S"
scc -S -o "$tmp/scc.s" "$tmp/smallc.c" || exit 1
as -o "$tmp/scc.o" "$tmp/scc.s" || exit 1
ld -r -o "$tmp/scc.ro" "$tmp/scc.o" || exit 1
test -f "$tmp/scc.ro" || exit 1

echo "step 2: smlrc direct"
/usr/libexec/smlrc "$tmp/ansi.c" "$tmp/smlrc.s" || exit 1
as -o "$tmp/smlrc.o" "$tmp/smlrc.s" || exit 1
ld -r -o "$tmp/smlrc.ro" "$tmp/smlrc.o" || exit 1
test -f "$tmp/smlrc.ro" || exit 1

echo "step 3: lcc -S"
lcc -S -o "$tmp/lcc.s" "$tmp/ansi.c" || exit 1
as -o "$tmp/lcc.o" "$tmp/lcc.s" || exit 1
ld -r -o "$tmp/lcc.ro" "$tmp/lcc.o" || exit 1
test -f "$tmp/lcc.ro" || exit 1

echo "secondary compiler smoke ok"
