#!/bin/sh
#
# Shell repro for command substitution with a pipe.  The
# assembler smoke uses this path before the first expected-reject case.
#

cd /tmp || cd /var/tmp || exit 1

src=sh-comsubst-smoke.$$.s
obj=sh-comsubst-smoke.$$.o
as_flags='-EB -mips3 -march=vr4300'
data_directive=.half
half_pattern='*022064377376126170232274*'

macros=`cc -dM -E - </dev/null 2>/dev/null`
case "$macros" in
*__i386__*)
	as_flags='--32 -march=i386'
	data_directive=.short
	half_pattern='*064022376377170126274232*'
	;;
*__MIPSEL__*)
	as_flags='-EL -mips3 -march=vr4300'
	half_pattern='*064022376377170126274232*'
	;;
esac

rm -f $src $obj
echo ".data" > $src
echo "halfprobe:" >> $src
echo "	$data_directive 0x1234" >> $src
echo "	$data_directive 0xfffe" >> $src
echo "	$data_directive 0x5678" >> $src
echo "	$data_directive 0x9abc" >> $src

as $as_flags -o $obj $src || exit 1

od -b $obj >/dev/null || {
	echo "sh-comsubst-smoke: od failed" >&2
	rm -f $src $obj
	exit 1
}

for i in 0 1 2 3 4 5 6 7
do
	echo "sh-comsubst-smoke: iteration $i"
	bytes=`od -b $obj | tr -d ' \n'`
	case "$bytes" in
	$half_pattern) ;;
	*)
		echo "sh-comsubst-smoke: bad .half bytes" >&2
		od -b $obj
		rm -f $src $obj
		exit 1
		;;
	esac
	echo "sh-comsubst-smoke: parent shell survived $i"
done

rm -f $src $obj
echo "sh-comsubst-smoke ok"
