#!/bin/sh
#
# Exercise a slightly larger native make workload than build-workload-smoke:
# two static libraries, a dependent -L/-l link order, nm/size/archive checks,
# and an explicit clean/rebuild cycle for both native compiler driver names.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

cd /var/tmp || exit 1

base=make-workload-smoke.$$
dir=$base.dir

rm -rf "$dir"
mkdir "$dir" || exit 1
cd "$dir" || exit 1
mkdir inc lib obj bin src || exit 1

cleanup()
{
	cd /var/tmp
	rm -rf "$dir"
}

fail()
{
	echo "make-workload-smoke fail: $1"
	cleanup
	exit 1
}

trap 'fail interrupted' 1 2 3 15

cat > inc/mkwork.h <<'EOF'
#ifndef MKWORK_H
#define MKWORK_H

unsigned long calc_series(int n);
unsigned long calc_mix(const unsigned long *values, int n);
int fmt_line(char *buf, unsigned int len, const char *tag);

#endif
EOF

cat > src/calc.c <<'EOF'
#include "mkwork.h"

unsigned long
calc_series(int n)
{
	unsigned long acc;
	int i;

	acc = 7UL;
	for (i = 0; i != n; ++i)
		acc = acc * 33UL + (unsigned long)i * 17UL +
		    (unsigned long)(i & 3);
	return acc;
}

unsigned long
calc_mix(const unsigned long *values, int n)
{
	unsigned long acc;
	int i;

	acc = 2166136261UL;
	for (i = 0; i != n; ++i) {
		acc ^= values[i] + (unsigned long)i;
		acc *= 16777619UL;
	}
	return acc;
}
EOF

cat > src/fmt.c <<'EOF'
#include <stdio.h>
#include "mkwork.h"

int
fmt_line(char *buf, unsigned int len, const char *tag)
{
	unsigned long values[4];
	unsigned long series;
	unsigned long mix;

	series = calc_series(12);
	values[0] = calc_series(3);
	values[1] = calc_series(5);
	values[2] = 0x1234abcdUL;
	values[3] = series;
	mix = calc_mix(values, 4);
	sprintf(buf, "MKWORK:%s:%lu:%lu", tag, series, mix);
	return (int)len;
}
EOF

cat > src/main.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include "mkwork.h"

int
main(void)
{
	char line[80];
	int rc;

	memset(line, 0, sizeof(line));
	rc = fmt_line(line, sizeof(line), "ok");
	if (rc != (int)sizeof(line))
		return 2;
	if (strcmp(line, "MKWORK:ok:1223466235:3419191017") != 0) {
		printf("make-workload-smoke bad line: %s\n", line);
		return 3;
	}
	printf("%s\n", line);
	return 0;
}
EOF

cat > src/probe.c <<'EOF'
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
	struct stat st;
	FILE *fp;
	char magic[8];

	if (argc != 2)
		return 2;
	if (stat(argv[1], &st) < 0) {
		perror(argv[1]);
		return 3;
	}
	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		perror(argv[1]);
		return 4;
	}
	if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic)) {
		fclose(fp);
		return 5;
	}
	fclose(fp);
	if (memcmp(magic, "!<arch>\n", 8) != 0)
		return 6;
	printf("archive-ok:%s:%ld\n", argv[1], (long)st.st_size);
	return 0;
}
EOF

cat > Makefile <<'EOF'
CC=cc
CFLAGS=-O
AR=ar
RANLIB=ranlib

all: bin/mkwork bin/probe

obj/calc.o: src/calc.c inc/mkwork.h
	$(CC) $(CFLAGS) -Iinc -c -o obj/calc.o src/calc.c

obj/fmt.o: src/fmt.c inc/mkwork.h
	$(CC) $(CFLAGS) -Iinc -c -o obj/fmt.o src/fmt.c

obj/main.o: src/main.c inc/mkwork.h
	$(CC) $(CFLAGS) -Iinc -c -o obj/main.o src/main.c

obj/probe.o: src/probe.c
	$(CC) $(CFLAGS) -Iinc -c -o obj/probe.o src/probe.c

lib/libcalc.a: obj/calc.o
	$(AR) r lib/libcalc.a obj/calc.o
	$(RANLIB) lib/libcalc.a

lib/libfmt.a: obj/fmt.o
	$(AR) r lib/libfmt.a obj/fmt.o
	$(RANLIB) lib/libfmt.a

bin/mkwork: obj/main.o lib/libfmt.a lib/libcalc.a
	$(CC) $(CFLAGS) -o bin/mkwork obj/main.o -Llib -lfmt -lcalc

bin/probe: obj/probe.o
	$(CC) $(CFLAGS) -o bin/probe obj/probe.o

check: all
	bin/mkwork > run.out
	grep '^MKWORK:ok:1223466235:3419191017$$' run.out >/dev/null
	nm lib/libcalc.a > calc.nm
	grep calc_series calc.nm >/dev/null
	nm lib/libfmt.a > fmt.nm
	grep fmt_line fmt.nm >/dev/null
	size bin/mkwork > mkwork.size
	bin/probe lib/libcalc.a > probe.out
	grep '^archive-ok:lib/libcalc.a:' probe.out >/dev/null

clean:
	rm -f obj/*.o lib/*.a bin/mkwork bin/probe \
	    run.out calc.nm fmt.nm mkwork.size probe.out
EOF

run_one()
{
	cc=$1
	label=$2

	echo "make-workload-smoke: $label"
	make clean >/dev/null 2>&1 || fail "$label initial clean"
	make CC="$cc" CFLAGS="-O" check || fail "$label check"
	make clean || fail "$label clean"
	make CC="$cc" CFLAGS="-O" all || fail "$label rebuild"
	test -x bin/mkwork || fail "$label rebuild output"
	bin/mkwork > "$label.rebuild.out" || fail "$label rebuild run"
	grep '^MKWORK:ok:1223466235:3419191017$' "$label.rebuild.out" \
	    >/dev/null || fail "$label rebuild output"
	make clean || fail "$label final clean"
}

run_one /usr/bin/cc cc
run_one /usr/bin/pcc pcc

echo "make workload smoke ok"
cleanup
exit 0
