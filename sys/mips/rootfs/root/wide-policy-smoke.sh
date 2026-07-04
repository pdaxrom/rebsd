#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

cd /var/tmp || exit 1

run_one()
{
	cc=$1
	out=$2

	echo "wide-policy-smoke: $cc"
	rm -f "$out"
	$cc -O -o "$out" /root/wide-policy-smoke.c || exit 1
	./"$out" || exit 1
	rm -f "$out"
}

run_one /usr/bin/cc wide-policy-cc
run_one /usr/bin/pcc wide-policy-pcc

echo "wide policy smoke ok"
exit 0
