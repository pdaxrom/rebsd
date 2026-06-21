#!/bin/sh
#
# Narrow N64 shell repro for command substitution with a pipe.  The
# assembler smoke uses this path before the first expected-reject case.
#

cd /tmp || cd /var/tmp || exit 1

src=sh-comsubst-smoke.$$.s
obj=sh-comsubst-smoke.$$.o

rm -f $src $obj
echo ".data" > $src
echo "halfprobe:" >> $src
echo '	.half 0x1234' >> $src
echo '	.half 0xfffe' >> $src
echo '	.half 0x5678' >> $src
echo '	.half 0x9abc' >> $src

as -EB -mips3 -march=vr4300 -o $obj $src || exit 1

i=0
while test $i -lt 8
do
	echo "sh-comsubst-smoke: iteration $i"
	bytes=`od -b $obj | tr -d ' \n'`
	case "$bytes" in
	*022064377376126170232274*) ;;
	*)
		echo "sh-comsubst-smoke: bad .half bytes" >&2
		od -b $obj
		rm -f $src $obj
		exit 1
		;;
	esac
	echo "sh-comsubst-smoke: parent shell survived $i"
	i=`expr $i + 1`
done

rm -f $src $obj
echo "sh-comsubst-smoke ok"
