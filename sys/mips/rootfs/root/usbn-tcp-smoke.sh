#!/bin/sh
#
# USB Ethernet TCP smoke.  For the default static test, configure the host USB
# Ethernet side as 10.64.0.1 and run the repository TCP echo service there
# before starting this test:
#
#      tools/n64usbnet/n64usbnet-echo 10.64.0.1 2323
#
# After DHCP, pass the DHCP router address explicitly:
#
#      /root/usbn-tcp-smoke.sh 192.168.2.1
#

echo "usbn-tcp-smoke diag v1"

peer=${1-10.64.0.1}
port=${2-2323}

cd /tmp || exit 1
base=usbn-tcp-smoke.$$
rm -f $base $base.s $base.o $base.ro

if test "$peer" = 10.64.0.1; then
        echo "step 1: configure usbn0"
        /sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up || exit 1
else
        echo "step 1: inspect usbn0"
        /sbin/ifconfig usbn0 || exit 1
fi

echo "step 2: ping USB peer"
/usr/bin/ping -c 1 $peer || exit 1

echo "step 3: build TCP USB smoke"
cc -S -o $base.s /root/usbn-tcp-smoke.c || exit 1
as -o $base.o $base.s || exit 1
ld -r -o $base.ro $base.o || exit 1
cc -o $base /root/usbn-tcp-smoke.c || exit 1

echo "step 4: TCP echo through USB peer"
./$base $peer $port || exit 1

rm -f $base $base.s $base.o $base.ro
echo "usbn-tcp-smoke ok"
