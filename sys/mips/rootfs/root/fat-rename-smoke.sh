#!/bin/sh
#
# Creates one private 8.3 test directory, removes it on exit, and never
# overwrites a pre-existing test directory.  It mounts and unmounts the FAT
# volume itself.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

device=${1:-/dev/sd0a}
mnt=${2:-/mnt}
tmp=/var/tmp/fat-rename-smoke.$$
pat1=$tmp.1
pat2=$tmp.2
log=$tmp.log
ready=$tmp.ready
work=
src=
dst=
old=
new=
mounted=no
work_owned=no
hold=

cleanup()
{
	if test -n "$hold"; then
		kill "$hold" >/dev/null 2>&1
		wait "$hold" >/dev/null 2>&1
		hold=
	fi
	if test "$mounted" = yes; then
		if test "$work_owned" = yes; then
			rm -f "$src" "$dst" "$old/FILE.BIN" "$new/FILE.BIN"
			rmdir "$old" >/dev/null 2>&1
			rmdir "$new" >/dev/null 2>&1
			rmdir "$work" >/dev/null 2>&1
			sync
		fi
		umount "$mnt" >/dev/null 2>&1
		mounted=no
	fi
	rm -f "$pat1" "$pat2" "$log" "$ready"
}

fail()
{
	echo "fat-rename-smoke fail: $1" >&2
	exit 1
}

absent()
{
	if test -e "$1"; then
		fail "$1 already exists"
	fi
}

trap cleanup 0
trap 'fail interrupted' 1 2 3 15

echo "fat-rename-smoke: mount $device on $mnt"
mount -t fat "$device" "$mnt" || fail mount
mounted=yes

for leaf in RNTEST0 RNTEST1 RNTEST2 RNTEST3 RNTEST4 RNTEST5 RNTEST6 RNTEST7 \
    RNTEST8 RNTEST9 RNTESTA RNTESTB RNTESTC RNTESTD RNTESTE RNTESTF; do
	if mkdir "$mnt/$leaf" >/dev/null 2>&1; then
		work=$mnt/$leaf
		work_owned=yes
		break
	fi
done
test -n "$work" || fail "no free private test directory name"

src=$work/SRC.BIN
dst=$work/DST.BIN
old=$work/OLD
new=$work/NEW

test -e "$work" || fail "shell test -e missed an existing path"
test ! -e "$work/ABSENT" || fail "shell test -e found an absent path"

echo "fat-rename-smoke: verify ls failure status"
if ls "$work/ABSENT" >/dev/null 2>&1; then
	fail "ls returned success for an absent path"
fi

dd if=/bin/sh of="$pat1" bs=1024 count=3 >/dev/null 2>&1 ||
	fail "create pattern 1"
dd if=/bin/sh of="$pat2" bs=1024 skip=3 count=2 >/dev/null 2>&1 ||
	fail "create pattern 2"

echo "fat-rename-smoke: rename regular file"
dd if="$pat1" of="$src" bs=1024 count=3 >/dev/null 2>&1 ||
	fail "create source"
mv "$src" "$dst" || fail "rename source"
absent "$src"
cmp "$pat1" "$dst" || fail "renamed data"

echo "fat-rename-smoke: replace regular target"
dd if="$pat2" of="$src" bs=1024 count=2 >/dev/null 2>&1 ||
	fail "create replacement source"
mv "$src" "$dst" || fail "replace target"
absent "$src"
cmp "$pat2" "$dst" || fail "replacement data"

echo "fat-rename-smoke: protect open target"
dd if="$pat1" of="$src" bs=1024 count=3 >/dev/null 2>&1 ||
	fail "create busy source"
rm -f "$ready"
(
	echo ready > "$ready" || exit 1
	sleep 30
) < "$dst" &
hold=$!
for wait_try in 1 2 3 4 5; do
	if test -e "$ready"; then
		break
	fi
	sleep 1
done
test -e "$ready" || fail "target holder did not become ready"
if mv "$src" "$dst" >"$log" 2>&1; then
	fail "open target was replaced"
fi
cmp "$pat1" "$src" || fail "busy source changed"
cmp "$pat2" "$dst" || fail "busy target changed"
kill "$hold" >/dev/null 2>&1 || fail "stop target holder"
wait "$hold" >/dev/null 2>&1
hold=
mv "$src" "$dst" || fail "replace released target"
cmp "$pat1" "$dst" || fail "released target data"

echo "fat-rename-smoke: rename non-empty directory"
mkdir "$old" || fail "mkdir old"
dd if="$pat2" of="$old/FILE.BIN" bs=1024 count=2 >/dev/null 2>&1 ||
	fail "create directory file"
mv "$old" "$new" || fail "rename directory"
absent "$old"
cmp "$pat2" "$new/FILE.BIN" || fail "renamed directory data"

echo "fat-rename-smoke: sync and remount"
sync
umount "$mnt" || fail "first unmount"
mounted=no
mount -t fat "$device" "$mnt" || fail remount
mounted=yes
cmp "$pat1" "$dst" || fail "file after remount"
cmp "$pat2" "$new/FILE.BIN" || fail "directory after remount"

rm "$dst" || fail "remove renamed file"
rm "$new/FILE.BIN" || fail "remove directory file"
rmdir "$new" || fail "remove renamed directory"
rmdir "$work" || fail "remove private test directory"
sync
work_owned=no
umount "$mnt" || fail "final unmount"
mounted=no

echo "fat rename smoke ok"
exit 0
