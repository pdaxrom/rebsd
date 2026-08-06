#!/bin/sh
set -e

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
cd /var/tmp
tmp=/var/tmp/pcc-smoke.$$
trap 'rm -f "$tmp" "$tmp.s" "$tmp.o" "$tmp.ro" "$tmp.i"' 0 1 2 3 15

for tool in /usr/bin/cc /usr/bin/pcc /usr/bin/cpp /usr/bin/as \
	/usr/bin/ld /usr/bin/ar /usr/bin/ranlib /usr/bin/nm \
	/usr/libexec/pcc/cpp /usr/libexec/pcc/ccom; do
	test -x "$tool"
done

echo PCC_I686_SMOKE_BEGIN
echo PCC_I686_SMOKE_STEP cpp
cpp /root/pcc-smoke.c > "$tmp.i"
grep 'i686 pcc smoke ok' "$tmp.i" >/dev/null
echo PCC_I686_SMOKE_STEP cc-S
cc -v -S -o "$tmp.s" /root/pcc-smoke.c
echo PCC_I686_SMOKE_STEP as
as --32 -o "$tmp.o" "$tmp.s"
echo PCC_I686_SMOKE_STEP ld-r
ld -r -o "$tmp.ro" "$tmp.o"
test -s "$tmp.ro"
echo PCC_I686_SMOKE_STEP cc-link
cc -o "$tmp" /root/pcc-smoke.c
echo PCC_I686_SMOKE_STEP cc-run
"$tmp" | grep 'i686 pcc smoke ok' >/dev/null
echo PCC_I686_SMOKE_STEP pcc-link
pcc -o "$tmp" /root/pcc-smoke.c
echo PCC_I686_SMOKE_STEP pcc-run
"$tmp" | grep 'i686 pcc smoke ok' >/dev/null
echo PCC_I686_SELFHOST_OK
echo PCC_SMOKE_ALL_FAILURES 0
echo PCC_SMOKE_ALL_OK
