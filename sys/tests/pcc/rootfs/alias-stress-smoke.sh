#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

cd /var/tmp || exit 1

cleanup()
{
	rm -f alias-stress-cc alias-stress-cc.s alias-stress-cc.o \
	    alias-stress-cc.ro alias-stress-pcc alias-stress-pcc.s \
	    alias-stress-pcc.o alias-stress-pcc.ro
}

run_one()
{
	cc=$1
	label=$2

	echo "alias-stress-smoke: $cc"
	$cc -O -S -o alias-stress-$label.s /root/alias-stress-smoke.c ||
	    exit 1
	as -o alias-stress-$label.o alias-stress-$label.s || exit 1
	ld -r -o alias-stress-$label.ro alias-stress-$label.o || exit 1
	test -f alias-stress-$label.ro || exit 1
	$cc -O -o alias-stress-$label /root/alias-stress-smoke.c || exit 1
	./alias-stress-$label || exit 1
}

cleanup
run_one /usr/bin/cc cc
run_one /usr/bin/pcc pcc
cleanup
echo "alias stress smoke ok"
exit 0
