#!/bin/sh
#
# Non-destructive filesystem-tool smoke test.  It creates a sparse FAT16
# image in /var and optionally checks an unmounted FAT block device read-only.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

image=/var/fs-tools-smoke.$$.img
device=${1-}

cleanup()
{
	rm -f "$image"
}

fail()
{
	echo "fs-tools-smoke fail: $1" >&2
	exit 1
}

trap cleanup 0
trap 'fail interrupted' 1 2 3 15

echo "fs-tools-smoke: verify UFS compatibility names"
cmp /sbin/fsck /sbin/fsck.ufs >/dev/null 2>&1 ||
	fail "fsck.ufs differs from fsck"
cmp /sbin/mkfs /sbin/mkfs.ufs >/dev/null 2>&1 ||
	fail "mkfs.ufs differs from mkfs"

echo "fs-tools-smoke: create sparse 4 MiB image"
dd if=/dev/zero of="$image" bs=512 seek=8191 count=1 >/dev/null 2>&1 ||
	fail "create sparse image"

echo "fs-tools-smoke: format FAT16 image"
mkfs.fat -F 16 -n REBSDTEST "$image" || fail mkfs.fat

echo "fs-tools-smoke: check FAT16 image"
fsck.fat -n "$image" || fail fsck.fat

if test -n "$device"; then
	echo "fs-tools-smoke: read-only check of $device"
	fsck.fat -n "$device" || fail "fsck.fat $device"
fi

echo "filesystem tools smoke ok"
exit 0
