#!/bin/sh
#
# USB Ethernet DHCP smoke.  The host USB Ethernet side must run a DHCP server
# on the link before this test is started.
#

echo "usbn-dhcp-smoke diag v1"

echo "step 1: request lease on usbn0"
if /sbin/dhclient -v usbn0; then
        :
else
        echo "usbn-dhcp-smoke: dhclient failed"
        /sbin/ifconfig usbn0
        /usr/bin/netstat -i
        exit 1
fi

echo "step 2: inspect interface and lease"
/sbin/ifconfig usbn0 || exit 1
cat /var/run/dhclient.lease || exit 1

addr=`sed -n 's/^address //p' /var/run/dhclient.lease`
router=`sed -n 's/^router //p' /var/run/dhclient.lease`

test "x$addr" != x || exit 1
test "x$addr" != x0.0.0.0 || exit 1

if test "x$router" != x; then
        echo "step 3: ping DHCP router"
        /usr/bin/ping -c 1 $router || exit 1
else
        echo "step 3: no router in lease"
fi

echo "step 4: routes and resolver"
/usr/bin/netstat -rn || exit 1
cat /var/run/resolv.conf

echo "usbn-dhcp-smoke ok"
