#!/bin/sh
cd /var/tmp || exit 1

for tool in /usr/bin/cc /usr/bin/pcc /usr/bin/cpp \
    /usr/libexec/pcc/cpp /usr/libexec/pcc/ccom
do
    test -x "$tool" || exit 1
done

for unsupported in /usr/bin/p++ /usr/libexec/pcc/cxxcom
do
    if test -f "$unsupported"; then
        echo "unsupported C++ compiler still present: $unsupported"
        exit 2
    fi
done

for legacy in /usr/bin/lcc /usr/bin/scc /usr/libexec/ccom \
    /usr/libexec/lccom /usr/libexec/smallc /usr/libexec/smlrc
do
    if test -f "$legacy"; then
        echo "legacy compiler still present: $legacy"
        exit 2
    fi
done

tmp=/var/tmp/native-pcc-smoke.$$
rm -f "$tmp" "$tmp.c" "$tmp.i" "$tmp.s" "$tmp.o" "$tmp.pcc" "$tmp.out"
trap 'rc=$?; rm -f "$tmp" "$tmp.c" "$tmp.i" "$tmp.s" "$tmp.o" "$tmp.pcc" "$tmp.out"; exit $rc' 0 1 2 3 15

cat > "$tmp.c" <<'EOF'
#include <stdio.h>

static int state = 1;

static void __attribute__((constructor))
init_state(void)
{
    state = 41;
}

static void __attribute__((destructor))
done_state(void)
{
    printf("native-pcc-dtor:%d\n", state);
}

static int
add1(int value)
{
    return value + 1;
}

int
main(void)
{
    printf("native-pcc-main:%d\n", add1(state));
    return state == 41 ? 0 : 3;
}
EOF

echo "step 1: cpp"
/usr/bin/cpp "$tmp.c" > "$tmp.i" || exit 1
test -s "$tmp.i" || exit 1
grep native-pcc-main "$tmp.i" >/dev/null || exit 1

echo "step 2: cc -S"
cc -S -o "$tmp.s" "$tmp.c" || exit 1
test -s "$tmp.s" || exit 1

echo "step 3: cc -c"
cc -c -o "$tmp.o" "$tmp.c" || exit 1
test -s "$tmp.o" || exit 1

echo "step 4: cc link/run"
cc -v -o "$tmp" "$tmp.c" || exit 1
test -s "$tmp" || exit 1
"$tmp" > "$tmp.out" || exit 1
grep native-pcc-main:42 "$tmp.out" >/dev/null || exit 1
grep native-pcc-dtor:41 "$tmp.out" >/dev/null || exit 1

echo "step 5: pcc link/run"
pcc -o "$tmp.pcc" "$tmp.c" || exit 1
test -s "$tmp.pcc" || exit 1
"$tmp.pcc" > "$tmp.out" || exit 1
grep native-pcc-main:42 "$tmp.out" >/dev/null || exit 1
grep native-pcc-dtor:41 "$tmp.out" >/dev/null || exit 1

echo "native pcc smoke ok"
