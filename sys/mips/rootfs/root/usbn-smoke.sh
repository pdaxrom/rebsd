#!/bin/sh
#
# Malta fake USB Ethernet smoke.  The fake lower half emulates one peer at
# 10.64.0.1 and loops ARP/ICMP back through the generic if_usbn upper half.
#

echo "usbn-smoke diag v1"

cd /tmp || exit 1
base=usbn-smoke.$$
rm -f $base.if $base.rt

echo "step 1: configure usbn0"
/sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up || exit 1
/sbin/ifconfig usbn0 || exit 1

echo "step 2: ping fake USB peer"
/usr/bin/ping -c 1 10.64.0.1 || exit 1

echo "step 3: inspect interface and routes"
/usr/bin/netstat -i > $base.if || exit 1
grep '^usbn0' $base.if >/dev/null || {
	cat $base.if
	exit 1
}
/usr/bin/netstat -r > $base.rt || exit 1
grep '10.64.0' $base.rt >/dev/null || {
	cat $base.rt
	exit 1
}

rm -f $base.if $base.rt
echo "usbn-smoke ok"
