#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

iterations=${VM_STRESS_ITERATIONS:-8}
before=/var/tmp/vm-stress-before.$$
after=/var/tmp/vm-stress-after.$$

cleanup()
{
	rm -f "$before" "$after"
}

snapshot()
{
	sysctl -n \
	    vm.page_free \
	    vm.objects \
	    vm.anon_pages \
	    vm.object_resident \
	    vm.object_swapped \
	    vm.shm_objects \
	    vm.shm_pages \
	    vm.sysv_segments \
	    vm.sysv_attachments \
	    vm.pmap_mappings \
	    vm.pmap_resident
}

case "$iterations" in
0|*[!0-9]*)
	echo "vm-stress-smoke: invalid iteration count: $iterations" >&2
	exit 2
	;;
esac

trap cleanup 0 1 2 3 15

echo "vm-stress-smoke: warmup"
/root/vm-process-smoke || exit 1
snapshot > "$before" || exit 1

i=0
while test "$i" -lt "$iterations"
do
	i=`expr "$i" + 1`
	echo "vm-stress-smoke: iteration $i/$iterations"
	/root/vm-process-smoke || exit 1
done

snapshot > "$after" || exit 1
if ! cmp -s "$before" "$after"; then
	echo "vm-stress-smoke: VM counters leaked" >&2
	echo "before:" >&2
	cat "$before" >&2
	echo "after:" >&2
	cat "$after" >&2
	exit 1
fi

echo "vm stress smoke ok ($iterations iterations)"
exit 0
