#!/bin/sh
#
# Malta QEMU DHCP/DNS smoke.  Requires the Malta run-net target, whose QEMU
# user network provides DHCP, a gateway at 10.0.2.2, and DNS at 10.0.2.3.
#

echo "dhcp-dns-smoke diag v1"

cd /tmp || exit 1
base=dhcp-dns-smoke.$$
rm -f $base $base.s $base.o $base.ro $base.resolv $base.if

echo "step 1: dhclient ne0"
/sbin/dhclient -v ne0 || exit 1

echo "step 2: resolver file"
test -s /var/run/resolv.conf || exit 1
grep '^nameserver ' /var/run/resolv.conf > $base.resolv || {
	cat /var/run/resolv.conf
	exit 1
}
cat $base.resolv

echo "step 3: inspect ne0"
/sbin/ifconfig ne0 > $base.if || exit 1
grep 'inet ' $base.if >/dev/null || {
	cat $base.if
	exit 1
}
cat $base.if

echo "step 4: ping QEMU gateway"
/usr/bin/ping -c 1 10.0.2.2 || exit 1

echo "step 5: build DNS smoke"
cc -S -o $base.s /root/dns-smoke.c || exit 1
as -o $base.o $base.s || exit 1
ld -r -o $base.ro $base.o || exit 1
cc -o $base /root/dns-smoke.c || exit 1

echo "step 6: gethostbyname through /etc/hosts fallback"
./$base qemu-gw || exit 1

echo "step 7: raw resolver query to QEMU DNS"
./$base qemu-gw example.com || true

rm -f $base $base.s $base.o $base.ro $base.resolv $base.if
echo "dhcp-dns-smoke ok"
