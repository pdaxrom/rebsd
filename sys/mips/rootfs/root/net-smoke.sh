#!/bin/sh
echo "net-smoke diag v1"
cd /tmp || exit 1
/sbin/ifconfig lo0 || exit 1
/sbin/ifconfig lo0 inet 127.0.0.1 up || exit 1
/sbin/route delete host 127.0.0.2 127.0.0.1 >/dev/null 2>&1
/sbin/route add host 127.0.0.2 127.0.0.1 0 || exit 1
/sbin/route delete host 127.0.0.2 127.0.0.1 || exit 1
/usr/bin/ping -c 1 127.0.0.1 || exit 1
base=net-smoke.$$
for opt in -i -r -s -m -u; do
	/usr/bin/netstat $opt > $base.netstat 2>&1 || exit 1
	if egrep 'read error|bad read|not in namelist|no kernel namelist|cannot open|netstat: cannot' $base.netstat >/dev/null; then
		cat $base.netstat
		rm -f $base.netstat
		exit 1
	fi
	if test "$opt" = -i; then
		if ! egrep '^lo0[ *]' $base.netstat >/dev/null ||
		    egrep '^0\*' $base.netstat >/dev/null; then
			cat $base.netstat
			rm -f $base.netstat
			exit 1
		fi
	fi
done
/usr/bin/netstat -n > $base.netstat 2>&1 || exit 1
if egrep 'Memory fault|bad address|netstat: cannot' $base.netstat >/dev/null; then
	cat $base.netstat
	rm -f $base.netstat
	exit 1
fi
/usr/bin/netstat -p tcp > $base.netstat 2>&1 || exit 1
if egrep 'read error|bad read|not in namelist|no kernel namelist|cannot open|netstat: cannot' $base.netstat >/dev/null; then
	cat $base.netstat
	rm -f $base.netstat
	exit 1
fi
rm -f $base.netstat
rm -f $base $base.s $base.o $base.ro
cc -S -o $base.s /root/net-smoke.c || exit 1
as -o $base.o $base.s || exit 1
ld -r -o $base.ro $base.o || exit 1
cc -o $base /root/net-smoke.c || exit 1
./$base
status=$?
test $status -eq 0 || exit $status
/usr/bin/netstat -a -f inet > $base.timewait || exit 1
if ! egrep 'TIME_WAIT' $base.timewait >/dev/null; then
	cat $base.timewait
	rm -f $base $base.s $base.o $base.ro $base.timewait
	exit 1
fi
/usr/bin/netstat -m > $base.mbuf || exit 1
if egrep 'read error|bad read|not in namelist|no kernel namelist|cannot open|mbufs missing' $base.mbuf >/dev/null; then
	cat $base.mbuf
	rm -f $base $base.s $base.o $base.ro $base.timewait $base.mbuf
	exit 1
fi
if ! egrep '^0 requests for memory denied' $base.mbuf >/dev/null; then
	cat $base.mbuf
	rm -f $base $base.s $base.o $base.ro $base.timewait $base.mbuf
	exit 1
fi
if ! egrep '^0 requests for memory delayed' $base.mbuf >/dev/null; then
	cat $base.mbuf
	rm -f $base $base.s $base.o $base.ro $base.timewait $base.mbuf
	exit 1
fi
if ! egrep '^0 calls to protocol drain routines' $base.mbuf >/dev/null; then
	cat $base.mbuf
	rm -f $base $base.s $base.o $base.ro $base.timewait $base.mbuf
	exit 1
fi
rm -f $base $base.s $base.o $base.ro $base.timewait $base.mbuf
echo "net-smoke ok"
