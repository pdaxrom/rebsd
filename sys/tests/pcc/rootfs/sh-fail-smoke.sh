#!/bin/sh
#
# Shell/fork failure repro: an external command returns nonzero
# from an if-condition, and the parent shell must keep its allocator intact.
#

cd /tmp || cd /var/tmp || exit 1

src=sh-fail-smoke.$$.s
obj=sh-fail-smoke.$$.o

macros=`cc -dM -E - </dev/null 2>/dev/null`
case "$macros" in
*__i386__*)
	as_flags='--32 -march=i386'
	invalid_instruction='vmcall'
	invalid_name=vmcall
	;;
*)
	as_flags='-EB -mips3 -march=vr4300'
	invalid_instruction='movn $2,$3,$4'
	invalid_name=movn
	;;
esac

rm -f $src $obj
echo ".text" > $src
echo "start:" >> $src
echo "	$invalid_instruction" >> $src

for i in 0 1 2 3 4 5 6 7
do
	echo "sh-fail-smoke: iteration $i"
	if as $as_flags -o $obj $src
	then
		echo "sh-fail-smoke: assembler accepted invalid $invalid_name" >&2
		rm -f $src $obj
		exit 1
	fi
	echo "sh-fail-smoke: parent shell survived $i"
done

sh -c 'kill -2 $$'
rc=$?
if test "$rc" != 130
then
	echo "sh-fail-smoke: SIGINT status $rc, expected 130" >&2
	rm -f $src $obj
	exit 1
fi
echo "sh-fail-smoke: SIGINT status preserved"

rm -f $src $obj
echo "sh-fail-smoke ok"
