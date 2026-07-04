#!/bin/sh
#
# Native compiler build workload smoke.  This creates a small multi-file
# project, builds it through make, archives one object with ar/ranlib, links it
# with -L/-l, and runs the result with both cc and pcc.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

cd /var/tmp || exit 1

base=build-workload-smoke.$$
dir=$base.dir

rm -rf "$dir"
mkdir "$dir" || exit 1
cd "$dir" || exit 1

cleanup()
{
    cd /var/tmp
    rm -rf "$dir"
}

fail()
{
    rc=$1
    cleanup
    exit $rc
}

trap 'fail 1' 1 2 3 15

cat > work.h <<'EOF'
#ifndef WORK_H
#define WORK_H

#define WORK_ITEMS 8
#define WORK_BYTES 8

struct work_item {
    int id;
    long score;
    unsigned char data[WORK_BYTES];
};

struct work_pair {
    long left;
    long right;
};

typedef int (*work_op)(struct work_item *, int, unsigned long *);

void worklib_init(struct work_item *items, int n);
unsigned long worklib_checksum(const struct work_item *items, int n);
int worklib_scramble(struct work_item *items, int n, unsigned long *out);
int worklib_fold(struct work_item *items, int n, unsigned long *out);
struct work_pair worklib_pair(long seed, struct work_item item);

#endif
EOF

cat > worklib.c <<'EOF'
#include "work.h"

void
worklib_init(struct work_item *items, int n)
{
    struct work_item *p;
    int i, j;

    for (i = 0, p = items; i != n; ++i, ++p) {
        p->id = i + 1;
        p->score = 100 + i * 17;
        for (j = 0; j != WORK_BYTES; ++j)
            p->data[j] = (unsigned char)((i + 1) * (j + 3) + j * 5);
    }
}

unsigned long
worklib_checksum(const struct work_item *items, int n)
{
    const struct work_item *p;
    const unsigned char *bp;
    unsigned long sum;
    int i;

    sum = 2166136261UL;
    for (i = 0, p = items; i != n; ++i, ++p) {
        sum ^= (unsigned long)p->id;
        sum *= 16777619UL;
        sum ^= (unsigned long)p->score;
        sum *= 16777619UL;
        for (bp = p->data; bp != p->data + WORK_BYTES; ++bp) {
            sum ^= (unsigned long)*bp;
            sum *= 16777619UL;
        }
    }
    return sum;
}

struct work_pair
worklib_pair(long seed, struct work_item item)
{
    struct work_pair pair;

    pair.left = seed + item.score + item.data[0] + item.data[3];
    pair.right = seed - item.id + item.data[WORK_BYTES - 1];
    return pair;
}

int
worklib_scramble(struct work_item *items, int n, unsigned long *out)
{
    struct work_item *p;
    unsigned char *bp;
    int i;

    for (i = 0, p = items; i != n; ++i, ++p) {
        for (bp = p->data; bp != p->data + WORK_BYTES; ++bp) {
            *bp = (unsigned char)((*bp ^
                (p->id * 11 + (int)(bp - p->data) * 7)) + 3);
        }
    }
    *out = worklib_checksum(items, n);
    return n;
}

int
worklib_fold(struct work_item *items, int n, unsigned long *out)
{
    struct work_item *p;
    struct work_pair pair;
    unsigned long sum;
    int i;

    sum = 0;
    for (i = 0, p = items; i != n; ++i, ++p) {
        pair = worklib_pair(17, *p);
        sum += (unsigned long)(pair.left ^ pair.right) +
            (unsigned long)p->id;
    }
    *out = sum;
    return n * 2;
}
EOF

cat > main.c <<'EOF'
#include <stdio.h>
#include "work.h"

static int
fail_ulong(const char *tag, unsigned long got, unsigned long expect)
{
    printf("build-workload-smoke fail: %s got=%lu expect=%lu\n",
        tag, got, expect);
    return 1;
}

int
main(void)
{
    struct work_item items[WORK_ITEMS];
    struct work_item *first;
    struct work_item *last;
    struct work_pair pair;
    work_op ops[2];
    unsigned long marker;
    unsigned long sum;
    unsigned char *bp;
    unsigned int byte_sum;
    int i;
    int rc;

    worklib_init(items, WORK_ITEMS);
    first = items;
    last = &items[WORK_ITEMS - 1];
    if (last - first != WORK_ITEMS - 1)
        return fail_ulong("pointer-diff", (unsigned long)(last - first),
            WORK_ITEMS - 1);
    if ((char *)(first + 1) - (char *)first !=
        (long)sizeof(struct work_item))
        return fail_ulong("byte-diff",
            (unsigned long)((char *)(first + 1) - (char *)first),
            (unsigned long)sizeof(struct work_item));

    sum = worklib_checksum(items, WORK_ITEMS);
    if (sum != 2668490221UL)
        return fail_ulong("initial-checksum", sum, 2668490221UL);

    pair = worklib_pair(50, items[2]);
    if (pair.left != 226 || pair.right != 112)
        return fail_ulong("struct-return",
            (unsigned long)(pair.left + pair.right), 338UL);

    marker = 0x11223344UL;
    bp = (unsigned char *)&marker;
    byte_sum = 0;
    for (i = 0; i != (int)sizeof(marker); ++i)
        byte_sum += bp[i];
    if (byte_sum != 170)
        return fail_ulong("char-alias", byte_sum, 170UL);

    ops[0] = worklib_scramble;
    ops[1] = worklib_fold;

    rc = (*ops[0])(items, WORK_ITEMS, &sum);
    if (rc != WORK_ITEMS)
        return fail_ulong("scramble-rc", rc, WORK_ITEMS);
    if (sum != 2294087981UL)
        return fail_ulong("scramble-checksum", sum, 2294087981UL);

    rc = (*ops[1])(items, WORK_ITEMS, &sum);
    if (rc != WORK_ITEMS * 2)
        return fail_ulong("fold-rc", rc, WORK_ITEMS * 2);
    if (sum != 2076UL)
        return fail_ulong("fold", sum, 2076UL);

    printf("build-workload-ok:%lu:%lu\n", 2294087981UL, sum);
    return 0;
}
EOF

cat > arstat.c <<'EOF'
#include <sys/stat.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    struct stat st;

    if (argc != 2)
        return 2;
    if (stat(argv[1], &st) < 0) {
        perror(argv[1]);
        return 3;
    }
    printf("arstat path=%s size=%ld mode=%o blocks=%ld ino=%lu\n",
        argv[1], (long)st.st_size, st.st_mode, (long)st.st_blocks,
        (unsigned long)st.st_ino);
    return 0;
}
EOF

cat > Makefile <<'EOF'
CC=cc
CFLAGS=-O

all: build-workload

build-workload: main.o libwork.a
	$(CC) $(CFLAGS) -o build-workload main.o -L. -lwork

main.o: main.c work.h
	$(CC) $(CFLAGS) -c -o main.o main.c

worklib.o: worklib.c work.h
	$(CC) $(CFLAGS) -c -o worklib.o worklib.c

arstat: arstat.c
	$(CC) $(CFLAGS) -o arstat arstat.c

libwork.a: worklib.o arstat
	nm worklib.o > worklib.pre-nm
	grep worklib_checksum worklib.pre-nm >/dev/null
	ar r libwork.a worklib.o
	ar t libwork.a > libwork.members || (echo "build-workload-smoke: ar t failed"; ./arstat worklib.o; ls -l worklib.o libwork.a; od -c libwork.a; exit 1)
	ranlib libwork.a || (echo "build-workload-smoke: ranlib failed"; cat worklib.pre-nm; cat libwork.members; exit 1)

clean:
	rm -f build-workload arstat main.o worklib.o libwork.a \
	    cc.out cc.nm cc.size pcc.out pcc.nm pcc.size
EOF

run_one()
{
    compiler=$1
    label=$2

    echo "build-workload-smoke: $label"
    make clean >/dev/null 2>&1
    make CC="$compiler" CFLAGS="-O" all || fail 1
    test -x build-workload || fail 1
    ./build-workload > "$label.out"
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "build-workload-smoke: $label run failed rc=$rc"
        cat "$label.out"
        fail 1
    fi
    grep build-workload-ok "$label.out" >/dev/null || fail 1
    nm libwork.a > "$label.nm" || fail 1
    grep worklib_checksum "$label.nm" >/dev/null || fail 1
    size build-workload > "$label.size" || fail 1
    make clean || fail 1
}

run_one cc cc
run_one pcc pcc

echo "build workload smoke ok"
cleanup
