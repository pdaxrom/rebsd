#!/bin/sh
echo "dhcp-recv-smoke diag v1"
cd /tmp || exit 1
iface=${1:-ne0}
base=dhcp-recv-smoke.$$
rm -f $base $base.s $base.o $base.ro
/sbin/ifconfig $iface inet 0.0.0.0 netmask 0.0.0.0 up || exit 1
cc -S -o $base.s /root/dhcp-recv-smoke.c || exit 1
as -o $base.o $base.s || exit 1
ld -r -o $base.ro $base.o || exit 1
cc -o $base /root/dhcp-recv-smoke.c || exit 1
./$base $iface
status=$?
/usr/bin/netstat -s
rm -f $base $base.s $base.o $base.ro
exit $status
