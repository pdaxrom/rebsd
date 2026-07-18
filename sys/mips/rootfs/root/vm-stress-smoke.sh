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

trap 'rc=$?; cleanup; exit $rc' 0 1 2 3 15

echo "vm-stress-smoke: warmup"
# Exercise the loop path before taking the baseline.  This old shell may
# lazily allocate its first anonymous heap page while saving expr's output;
# that page belongs to this long-lived test harness, not to the child process
# under test.
i=0
i=`expr "$i" + 1` || exit 1
/root/vm-process-smoke || exit 1
sleep 1 || exit 1
snapshot > "$before" || exit 1

i=0
while test "$i" -lt "$iterations"
do
	i=`expr "$i" + 1`
	echo "vm-stress-smoke: iteration $i/$iterations"
	/root/vm-process-smoke || exit 1
done

# Process teardown can become visible to the accounting sysctls one scheduler
# tick after wait(2) returns.  Use the same quiescence window as the baseline
# so the comparison detects persistent leaks rather than transient cleanup.
sleep 1 || exit 1
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
