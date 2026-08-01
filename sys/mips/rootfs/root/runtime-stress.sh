#!/bin/sh
#
# Long-running runtime stress for Malta and N64.  It keeps exercising the
# user/kernel return path with fork/exec, timer sleeps, pipes, shell child
# commands, and kmem-reading diagnostics.  It writes only to /var/tmp.
#

echo "runtime-stress diag v1"

case "$1" in
"")
	reps="00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59"
	sleep_arg=1
	once=no
	;;
once)
	reps="00 01 02 03 04 05 06 07 08 09"
	sleep_arg=1
	once=yes
	;;
quick)
	reps="00 01 02 03 04"
	sleep_arg=0
	once=yes
	;;
*)
	echo "usage: runtime-stress.sh [once|quick]" >&2
	exit 1
	;;
esac

tmp=/var/tmp/runtime-stress.$$
rm -rf "$tmp"
mkdir "$tmp" || exit 1
trap 'rm -rf "$tmp"' 0 1 2 3 15

while :
do
	echo "runtime-stress: sweep start"
	date || exit 1

	for n in $reps
	do
		date > "$tmp/date.out" || exit 1
		cat "$tmp/date.out" >/dev/null || exit 1

		echo "runtime-stress $n" > "$tmp/io.txt" || exit 1
		cat "$tmp/io.txt" | wc > "$tmp/wc.out" || exit 1
		cat "$tmp/wc.out" >/dev/null || exit 1

		ls / >/dev/null || exit 1
		ps ax >/dev/null || exit 1
		vmstat >/dev/null || exit 1
		w >/dev/null || exit 1
		/usr/sbin/pstat -T >/dev/null || exit 1

		if false; then
			echo "runtime-stress: false returned success" >&2
			exit 1
		fi
		true || exit 1

		sh -c 'date >/dev/null' || exit 1

		if test "$sleep_arg" = 1; then
			sleep 1 || exit 1
		fi
	done

	rm -f "$tmp/date.out" "$tmp/io.txt" "$tmp/wc.out"
	echo "runtime-stress: sweep ok"
	date || exit 1

	if test "$once" = yes; then
		exit 0
	fi
done
