#!/bin/sh
#
# Narrow N64 shell/fork failure repro: an external command returns nonzero
# from an if-condition, and the parent shell must keep its allocator intact.
#

cd /tmp || cd /var/tmp || exit 1

src=sh-fail-smoke.$$.s
obj=sh-fail-smoke.$$.o

rm -f $src $obj
echo ".text" > $src
echo "start:" >> $src
echo '	movn $2,$3,$4' >> $src

i=0
while test $i -lt 8
do
	echo "sh-fail-smoke: iteration $i"
	if as -EB -mips3 -march=vr4300 -o $obj $src
	then
		echo "sh-fail-smoke: assembler accepted invalid movn" >&2
		rm -f $src $obj
		exit 1
	fi
	echo "sh-fail-smoke: parent shell survived $i"
	i=`expr $i + 1`
done

rm -f $src $obj
echo "sh-fail-smoke ok"
