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
rm -f "$tmp" "$tmp.c" "$tmp.i" "$tmp.s" "$tmp.o" "$tmp.pcc" \
    "$tmp.ctime" "$tmp.ctime.c" "$tmp.freopen" "$tmp.freopen.c" \
    "$tmp.freopen.out" "$tmp.out"
trap 'rc=$?; rm -f "$tmp" "$tmp.c" "$tmp.i" "$tmp.s" "$tmp.o" "$tmp.pcc" "$tmp.ctime" "$tmp.ctime.c" "$tmp.freopen" "$tmp.freopen.c" "$tmp.freopen.out" "$tmp.out"; exit $rc' 0 1 2 3 15

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

cat > "$tmp.ctime.c" <<'EOF'
#include <stdio.h>
#include <time.h>

int
main(void)
{
    time_t t;
    struct tm *tm;
    char *s;

    t = 1;
    s = ctime(&t);
    tm = localtime(&t);
    if (!s || !tm)
        return 2;
    printf("native-pcc-ctime-1:%s", s);
    printf("native-pcc-localtime-1:%d:%d:%d:%d\n",
        tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_isdst);
    if (tm->tm_isdst != 0 && tm->tm_isdst != 1)
        return 3;

    t = 24L * 60L * 60L * 365L;
    s = ctime(&t);
    tm = localtime(&t);
    if (!s || !tm)
        return 4;
    printf("native-pcc-ctime-2:%s", s);
    printf("native-pcc-localtime-2:%d:%d:%d:%d\n",
        tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_isdst);
    if (tm->tm_isdst != 0 && tm->tm_isdst != 1)
        return 5;

    return 0;
}
EOF

echo "step 6: ctime/localtime link/run"
cc -o "$tmp.ctime" "$tmp.ctime.c" || exit 1
"$tmp.ctime" > "$tmp.out" || exit 1
grep native-pcc-ctime-1 "$tmp.out" >/dev/null || exit 1
grep native-pcc-localtime-2 "$tmp.out" >/dev/null || exit 1

cat > "$tmp.freopen.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    char *p;
    int i, j, n;

    if (argc != 2)
        return 2;

    fprintf(stdout, "native-pcc-freopen-before\n");
    fflush(stdout);
    if (freopen(argv[1], "w", stdout) == NULL)
        return 3;

    fprintf(stdout, "native-pcc-freopen-after\n");
    for (i = 0; i != 64; ++i) {
        n = 32 + i;
        p = malloc(n);
        if (p == NULL)
            return 4;
        memset(p, 0x5a, n);
        for (j = 0; j != n; ++j) {
            if ((unsigned char)p[j] != 0x5a)
                return 5;
        }
        free(p);
    }
    fprintf(stdout, "native-pcc-freopen-malloc-ok\n");
    fflush(stdout);
    return ferror(stdout) ? 6 : 0;
}
EOF

echo "step 7: stdout freopen/malloc link/run"
cc -o "$tmp.freopen" "$tmp.freopen.c" || exit 1
"$tmp.freopen" "$tmp.freopen.out" > "$tmp.out" || exit 1
grep native-pcc-freopen-before "$tmp.out" >/dev/null || exit 1
grep native-pcc-freopen-after "$tmp.freopen.out" >/dev/null || exit 1
grep native-pcc-freopen-malloc-ok "$tmp.freopen.out" >/dev/null || exit 1

echo "native pcc smoke ok"
