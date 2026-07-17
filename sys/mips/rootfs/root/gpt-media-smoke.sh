#!/bin/sh
#
# Guarded Ci20 storage test for a removable FAT disk.  "preflight" is
# read-only.  "migrate" converts one primary FAT MBR partition to GPT without
# moving its payload, then exercises the block layer and FAT filesystem.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

mode=$1
disk=$2
part=$3
mnt=$4
confirm=$5
test -n "$mode" || mode=preflight
test -n "$disk" || disk=/dev/rsd0
test -n "$part" || part=/dev/sd0a
test -n "$mnt" || mnt=/mnt
case "$part" in
/dev/sd*)
	rawpart=/dev/r`basename "$part"`
	;;
*)
	rawpart=$part
	;;
esac
tmp=/var/gpt-media-smoke.$$
mbr=$tmp.mbr
mbrout=$tmp.fdisk
gptout=$tmp.gpt
mountout=$tmp.mount
before=$tmp.before
after=$tmp.after
partsample=$tmp.partition
mounted=no
migration_started=no
success=no

cleanup()
{
	if test "$mounted" = yes; then
		umount "$mnt" >/dev/null 2>&1
		mounted=no
	fi
	rm -f "$mbrout" "$gptout" "$mountout"
	rm -f "$before".first "$before".middle "$before".last
	rm -f "$after".first "$after".middle "$after".last
	rm -f "$partsample"
	if test "$success" = yes || test "$migration_started" = no; then
		rm -f "$mbr"
	else
		echo "gpt-media-smoke: original MBR retained in $mbr" >&2
		echo "gpt-media-smoke: keep it for diagnosis" >&2
	fi
}

fail()
{
	echo "gpt-media-smoke fail: $1" >&2
	exit 1
}

usage()
{
	echo "usage: gpt-media-smoke.sh preflight [disk [partition [mountpoint]]]" >&2
	echo "       gpt-media-smoke.sh migrate disk partition [mountpoint]" >&2
	echo "       gpt-media-smoke.sh verify [disk [partition [mountpoint]]]" >&2
	echo "       gpt-media-smoke.sh fresh disk partition mountpoint DESTROY" >&2
	exit 2
}

trap cleanup 0
trap 'fail interrupted' 1 2 3 15

case "$mode" in
preflight|migrate|verify|fresh)
	;;
*)
	usage
	;;
esac

if test "$mode" = migrate && test $# -lt 3; then
	usage
fi
if test "$mode" = fresh && { test $# -ne 5 || test "$confirm" != DESTROY; }; then
	usage
fi

ensure_unmounted()
{
	mount >"$mountout" || fail "list mounts"
	if grep "^$part on " "$mountout" >/dev/null 2>&1; then
		fail "$part is mounted"
	fi
	if grep " on $mnt " "$mountout" >/dev/null 2>&1; then
		fail "$mnt is already in use"
	fi
}

numeric()
{
	test -n "$1" && expr "$1" + 0 >/dev/null 2>&1
}

read_mbr_layout()
{
	fdisk -p "$disk" >"$mbrout" || fail "read legacy MBR"
	cat "$mbrout"
	media=`awk 'NR == 1 { for (i = 1; i <= NF; ++i) if ($i == "sectors") { print $(i - 1); exit } }' "$mbrout"`
	parts=`awk '$1 ~ /^[1-4]$/ { ++n } END { print n + 0 }' "$mbrout"`
	start=`awk '$1 == 1 { print $2; exit }' "$mbrout"`
	sectors=`awk '$1 == 1 { print $3; exit }' "$mbrout"`
	type=`awk '$1 == 1 { print $5; exit }' "$mbrout"`
	numeric "$media" || fail "cannot parse media size"
	numeric "$start" || fail "cannot parse partition start"
	numeric "$sectors" || fail "cannot parse partition size"
	test "$parts" = 1 || fail "expected exactly one primary MBR partition"
	case "$type" in
	06|0b|0B|0c|0C|0e|0E)
		;;
	*)
		fail "partition 1 is not a supported FAT MBR type (0x$type)"
		;;
	esac
	test "$start" -ge 34 || fail "partition overlaps primary GPT metadata"
	end=`expr "$start" + "$sectors" - 1` ||
		fail "calculate partition end"
	last_usable=`expr "$media" - 34` ||
		fail "calculate GPT usable range"
	test "$end" -le "$last_usable" ||
		fail "partition overlaps backup GPT metadata"
	middle=`expr "$start" + "$sectors" / 2` ||
		fail "calculate middle sector"
	last=`expr "$end" - 7` || fail "calculate last sample sector"
}

sample_payload()
{
	prefix=$1
	dd if="$disk" of="$prefix.first" bs=512 skip="$start" count=8 \
		>/dev/null 2>&1 || fail "read first payload sample"
	dd if="$disk" of="$prefix.middle" bs=512 skip="$middle" count=8 \
		>/dev/null 2>&1 || fail "read middle payload sample"
	dd if="$disk" of="$prefix.last" bs=512 skip="$last" count=8 \
		>/dev/null 2>&1 || fail "read last payload sample"
}

compare_payload()
{
	cmp "$before.first" "$after.first" >/dev/null ||
		fail "first payload sample changed"
	cmp "$before.middle" "$after.middle" >/dev/null ||
		fail "middle payload sample changed"
	cmp "$before.last" "$after.last" >/dev/null ||
		fail "last payload sample changed"
}

check_raw_partition_mapping()
{
	echo "gpt-media-smoke: verify raw odd-sector partition mapping"
	dd if="$part" of="$partsample" bs=512 count=8 \
		>/dev/null 2>&1 || fail "read partition payload sample"
	cmp "$before.first" "$partsample" >/dev/null ||
		fail "raw disk and partition start address different sectors"
}

check_fat_read_only()
{
	echo "gpt-media-smoke: check FAT without modifying it"
	fsck.fat -n "$part" || fail "fsck.fat -n"
	echo "gpt-media-smoke: mount FAT read-only"
	mount -t fat -r "$part" "$mnt" || fail "read-only FAT mount"
	mounted=yes
	ls "$mnt" >/dev/null || fail "list FAT root"
	umount "$mnt" || fail "read-only FAT unmount"
	mounted=no
}

check_off64()
{
	echo "gpt-media-smoke: check 64-bit block offsets"
	/root/off64-smoke.sh "$disk" || fail "off64 smoke"
}

verify_gpt()
{
	gpt -p "$disk" >"$gptout" || fail "print GPT"
	cat "$gptout"
	grep "primary GPT" "$gptout" >/dev/null ||
		fail "primary GPT not selected"
	if grep "repair recommended" "$gptout" >/dev/null; then
		fail "GPT redundancy is degraded"
	fi
	gpt_start=`awk '$1 == 1 { print $2; exit }' "$gptout"`
	gpt_sectors=`awk '$1 == 1 { print $4; exit }' "$gptout"`
	test "$gpt_start" = "$start" || fail "partition start changed"
	test "$gpt_sectors" = "$sectors" || fail "partition size changed"
	gpt -r "$disk" >"$gptout" || fail "GPT no-op repair check"
	grep "no repair needed" "$gptout" >/dev/null ||
		fail "consistent GPT was unexpectedly rewritten"
}

if test "$mode" = fresh; then
	echo "gpt-media-smoke: DESTROY all partitioning and filesystems on $disk"
	ensure_unmounted
	test -c "$rawpart" || fail "raw partition device $rawpart is missing"
	echo "gpt-media-smoke: create empty GPT"
	gpt -c "$disk" || fail "create empty GPT"
	echo "gpt-media-smoke: create aligned Microsoft Basic Data partition"
	gpt -a -b 2048 -l REBSDUSB "$disk" 1 || fail "create GPT partition"
	gpt -p "$disk" >"$gptout" || fail "print fresh GPT"
	cat "$gptout"
	start=`awk '$1 == 1 { print $2; exit }' "$gptout"`
	sectors=`awk '$1 == 1 { print $4; exit }' "$gptout"`
	numeric "$start" || fail "cannot parse fresh GPT partition start"
	numeric "$sectors" || fail "cannot parse fresh GPT partition size"
	verify_gpt
	echo "gpt-media-smoke: format FAT32 through $rawpart"
	mkfs.fat -F 32 -n REBSDUSB "$rawpart" || fail "format FAT32"
	echo "gpt-media-smoke: check fresh FAT32"
	fsck.fat -n "$rawpart" || fail "check fresh FAT32"
	echo "gpt-media-smoke: verify raw and block partition mapping"
	sample_payload "$before"
	check_raw_partition_mapping
	check_off64
	echo "gpt-media-smoke: smoke-test mounted FAT32 read/write"
	mount -t fat "$part" "$mnt" || fail "mount fresh FAT32"
	mounted=yes
	diskspeed -b 128 -m 1 "$mnt/REBSDSPD.BIN" ||
		fail "mounted FAT32 benchmark"
	umount "$mnt" || fail "unmount fresh FAT32"
	mounted=no
	echo "gpt-media-smoke: exercise FAT create/rename/replace/remove"
	/root/fat-rename-smoke.sh "$part" "$mnt" || fail "FAT rename smoke"
	echo "gpt-media-smoke: final FAT32 consistency check"
	fsck.fat -n "$rawpart" || fail "final fresh FAT32 check"
	success=yes
	echo "gpt media fresh-format smoke ok"
	exit 0
fi

if test "$mode" = verify; then
	echo "gpt-media-smoke: verify GPT on $disk and FAT on $part"
	ensure_unmounted
	gpt -p "$disk" >"$gptout" || fail "print GPT"
	start=`awk '$1 == 1 { print $2; exit }' "$gptout"`
	sectors=`awk '$1 == 1 { print $4; exit }' "$gptout"`
	test -n "$start" && test -n "$sectors" ||
		fail "GPT partition 1 missing"
	end=`expr "$start" + "$sectors" - 1` ||
		fail "calculate GPT partition end"
	middle=`expr "$start" + "$sectors" / 2` ||
		fail "calculate GPT partition middle"
	last=`expr "$end" - 7` || fail "calculate GPT last sample sector"
	cat "$gptout"
	grep "primary GPT" "$gptout" >/dev/null ||
		fail "primary GPT not selected"
	if grep "repair recommended" "$gptout" >/dev/null; then
		fail "GPT redundancy is degraded"
	fi
	gpt -r "$disk" >"$gptout" || fail "GPT no-op repair check"
	grep "no repair needed" "$gptout" >/dev/null ||
		fail "consistent GPT was unexpectedly rewritten"
	sample_payload "$before"
	check_raw_partition_mapping
	check_fat_read_only
	check_off64
	success=yes
	echo "gpt media verify ok"
	exit 0
fi

echo "gpt-media-smoke: inspect MBR on $disk"
ensure_unmounted
read_mbr_layout
echo "gpt-media-smoke: benchmark 8 MiB sequential read"
diskspeed -r -b 256 -m 8 "$disk" || fail "read benchmark"
echo "gpt-media-smoke: sample FAT payload at start, middle, and end"
sample_payload "$before"
check_raw_partition_mapping
echo "gpt-media-smoke: dry-run MBR to GPT migration"
gpt -m -n "$disk" >"$gptout" || fail "GPT migration preflight"
cat "$gptout"
grep "no data written" "$gptout" >/dev/null || fail "dry run not reported"
check_fat_read_only
check_off64

if test "$mode" = preflight; then
	success=yes
	echo "gpt media preflight ok; no sectors were written"
	exit 0
fi

echo "gpt-media-smoke: save original MBR"
dd if="$disk" of="$mbr" bs=512 count=1 >/dev/null 2>&1 ||
	fail "save original MBR"
migration_started=yes
echo "gpt-media-smoke: migrate MBR to GPT without moving payload"
gpt -m "$disk" || fail "MBR to GPT migration"
verify_gpt
echo "gpt-media-smoke: verify raw payload samples"
sample_payload "$after"
compare_payload
check_fat_read_only
check_off64
echo "gpt-media-smoke: exercise FAT create/rename/replace/remove"
/root/fat-rename-smoke.sh "$part" "$mnt" || fail "FAT rename smoke"
echo "gpt-media-smoke: final FAT consistency check"
fsck.fat -n "$part" || fail "final fsck.fat -n"

success=yes
echo "gpt media migration smoke ok"
exit 0
