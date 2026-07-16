#!/bin/sh
#
# Non-destructive target-side GPT utility smoke test.  It uses a sparse image
# in /var and never writes a block device.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

workdir=${TMPDIR:-/var}
image=$workdir/gpt-image-smoke.$$.img
output=$workdir/gpt-image-smoke.$$.out
before=$workdir/gpt-image-smoke.$$.before
after=$workdir/gpt-image-smoke.$$.after
prefix=$workdir/gpt-image-smoke.$$.prefix
prefix_after=$workdir/gpt-image-smoke.$$.prefix-after

cleanup()
{
	rm -f "$image" "$output" "$before" "$after" "$prefix" "$prefix_after"
}

fail()
{
	echo "gpt-image-smoke fail: $1" >&2
	exit 1
}

trap cleanup 0
trap 'fail interrupted' 1 2 3 15

echo "gpt-image-smoke: create sparse 4 MiB image"
dd if=/dev/zero of="$image" bs=512 seek=8191 count=1 >/dev/null 2>&1 ||
	fail "create sparse image"

echo "gpt-image-smoke: create and validate GPT"
gpt -c "$image" || fail "gpt create"
gpt -p "$image" >"$output" || fail "gpt print empty"
grep "primary GPT" "$output" >/dev/null || fail "primary GPT not reported"

echo "gpt-image-smoke: add partition"
gpt -a -s 2048 -l REBSDTEST "$image" 1 || fail "gpt add"
gpt -p "$image" >"$output" || fail "gpt print populated"
grep "2048 REBSDTEST" "$output" >/dev/null || fail "partition not found"

echo "gpt-image-smoke: delete partition"
gpt -d "$image" 1 || fail "gpt delete"
gpt -p "$image" >"$output" || fail "gpt print deleted"
if grep "REBSDTEST" "$output" >/dev/null; then
	fail "deleted partition remains"
fi

echo "gpt-image-smoke: migrate legacy FAT MBR without moving payload"
rm -f "$image"
dd if=/dev/zero of="$image" bs=512 seek=8191 count=1 >/dev/null 2>&1 ||
	fail "recreate sparse MBR image"
# The image is already zero-filled.  Patch only nonzero bytes so the old
# printf(1) never has to carry an embedded NUL in its format string.
printf '\014' |
	dd of="$image" bs=1 seek=450 conv=notrunc >/dev/null 2>&1 ||
	fail "write MBR partition type"
printf '\077' |
	dd of="$image" bs=1 seek=454 conv=notrunc >/dev/null 2>&1 ||
	fail "write MBR partition start"
printf '\100\037' |
	dd of="$image" bs=1 seek=458 conv=notrunc >/dev/null 2>&1 ||
	fail "write MBR partition length"
printf '\125\252' |
	dd of="$image" bs=1 seek=510 conv=notrunc >/dev/null 2>&1 ||
	fail "write MBR signature"
printf 'REBSD-MBR-PAYLOAD\n' |
	dd of="$image" bs=512 seek=63 conv=notrunc >/dev/null 2>&1 ||
	fail "write payload marker"
dd if="$image" of="$before" bs=512 count=34 >/dev/null 2>&1 ||
	fail "save preflight metadata"
dd if="$image" of="$prefix" bs=1 count=446 >/dev/null 2>&1 ||
	fail "save MBR prefix"
gpt -m -n "$image" >"$output" || fail "gpt migration preflight"
grep "no data written" "$output" >/dev/null || fail "dry run not reported"
dd if="$image" of="$after" bs=512 count=34 >/dev/null 2>&1 ||
	fail "read dry-run metadata"
cmp "$before" "$after" >/dev/null || fail "dry run changed metadata"
dd if="$image" of="$before" bs=512 skip=63 count=1 >/dev/null 2>&1 ||
	fail "save payload sector"
gpt -m "$image" >"$output" || fail "gpt MBR migration"
grep "payload sectors were not moved" "$output" >/dev/null ||
	fail "payload preservation not reported"
gpt -p "$image" >"$output" || fail "print migrated GPT"
grep "8000" "$output" >/dev/null || fail "migrated partition not found"
dd if="$image" of="$after" bs=512 skip=63 count=1 >/dev/null 2>&1 ||
	fail "read migrated payload sector"
cmp "$before" "$after" >/dev/null || fail "migration changed payload"
dd if="$image" of="$prefix_after" bs=1 count=446 >/dev/null 2>&1 ||
	fail "read protective MBR prefix"
cmp "$prefix" "$prefix_after" >/dev/null || fail "MBR prefix not preserved"

echo "gpt image smoke ok"
exit 0
