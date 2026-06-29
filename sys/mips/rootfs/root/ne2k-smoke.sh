#!/bin/sh
#
# Malta QEMU NE2K smoke.  Requires the Malta run-net target, which starts QEMU
# user networking with a guestfwd echo endpoint at 10.0.2.100:2323.
#

echo "ne2k-smoke diag v1"

cd /tmp || exit 1
base=ne2k-smoke.$$
rm -f $base $base.s $base.o $base.ro $base.if $base.rt

echo "step 1: configure ne0"
/sbin/ifconfig ne0 inet 10.0.2.15 netmask 255.255.255.0 up || exit 1
/sbin/route delete default 10.0.2.2 >/dev/null 2>&1
/sbin/route add default 10.0.2.2 1 || exit 1

echo "step 2: ping QEMU gateway"
/usr/bin/ping -c 1 10.0.2.2 || exit 1

echo "step 3: inspect interface and routes"
/usr/bin/netstat -i > $base.if || exit 1
grep '^ne0' $base.if >/dev/null || {
	cat $base.if
	exit 1
}
/usr/bin/netstat -r > $base.rt || exit 1
grep 'default' $base.rt >/dev/null || {
	cat $base.rt
	exit 1
}

echo "step 4: build TCP guestfwd smoke"
cc -S -o $base.s /root/ne2k-smoke.c || exit 1
as -o $base.o $base.s || exit 1
ld -r -o $base.ro $base.o || exit 1
cc -o $base /root/ne2k-smoke.c || exit 1

echo "step 5: TCP echo through QEMU guestfwd"
./$base || exit 1

rm -f $base $base.s $base.o $base.ro $base.if $base.rt
echo "ne2k-smoke ok"
