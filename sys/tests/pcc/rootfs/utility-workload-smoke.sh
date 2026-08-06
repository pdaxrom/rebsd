#!/bin/sh
#
# Build and run a small set of real ReBSD utilities from source with both
# native compiler driver names.  This keeps the workload small enough for N64
# while covering normal utility source, stdio file I/O, getopt, multi-source
# links, and a.out header parsing.  Completed binaries are removed as soon as
# their checks finish so active compiler temporaries retain headroom in the
# 1 MiB N64 /var file system.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

src=/root/utility-src
endian_cflag=-DTARGET_BIG_ENDIAN

macros=`cc -dM -E - </dev/null 2>/dev/null`
case "$macros" in
*__MIPSEL__*|*__i386__*)
	endian_cflag=-DTARGET_LITTLE_ENDIAN
	;;
esac

cd /var/tmp || exit 1

base=utility-workload-smoke.$$
dir=$base.dir

rm -rf "$dir"
mkdir "$dir" || exit 1
cd "$dir" || exit 1

cleanup()
{
	cd /var/tmp
	rm -rf "$dir"
}

fail()
{
	echo "utility-workload-smoke fail: $1"
	cleanup
	exit 1
}

trap 'fail interrupted' 1 2 3 15

cat > sample.txt <<'EOF'
ReBSD PCC utility workload
0123456789
EOF

run_compiler()
{
	cc=$1
	prefix=$2

	echo "utility-workload-smoke: $cc"

	rm -f basename sum size *.out

	$cc -O -o basename "$src"/basename.c ||
	    fail "$cc basename compile"
	./basename /usr/bin/pcc > basename.out ||
	    fail "$cc basename run"
	grep '^pcc$' basename.out >/dev/null ||
	    fail "$cc basename output"
	cp basename "$prefix-basename" ||
	    fail "$cc basename save"
	rm -f basename.out

	$cc -O -o sum "$src"/sum.c || fail "$cc sum compile"
	./sum sample.txt > sum.out || fail "$cc sum run"
	grep '^48897[ 	][ 	]*1$' sum.out >/dev/null ||
	    fail "$cc sum output"
	rm -f sum sum.out

	$cc -O $endian_cflag -I"$src" -o size \
	    "$src"/size.c "$src"/aoutio.c ||
	    fail "$cc size compile"
	./size ./basename > size.out || fail "$cc size run"
	grep '^text' size.out >/dev/null || fail "$cc size header"

	rm -f basename size size.out
}

run_compiler /usr/bin/cc cc
run_compiler /usr/bin/pcc pcc

/usr/bin/cmp cc-basename pcc-basename >/dev/null
if test $? = 0; then
	echo "utility-workload-smoke diag: basename binaries match"
fi

echo "utility workload smoke ok"
cleanup
exit 0
