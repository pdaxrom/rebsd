#!/bin/sh
#
# Smoke-test writable cartridge ROMFS through both VFS and romfsctl.
#

base=/cart/rfst$$
ctl=/ctl$$

echo "romfs-smoke: vfs create/read"
rm -f $base/a.txt $base/b.txt 2>/dev/null
rmdir $base 2>/dev/null
mkdir $base || exit 1
echo old >$base/a.txt || exit 1
got=`cat $base/a.txt` || exit 1
if test "$got" != old; then
        echo "romfs-smoke: vfs read mismatch" >&2
        exit 1
fi

echo "romfs-smoke: vfs rename-over-existing"
echo new >$base/b.txt || exit 1
mv $base/a.txt $base/b.txt || exit 1
got=`cat $base/b.txt` || exit 1
if test "$got" != old; then
        echo "romfs-smoke: rename mismatch" >&2
        exit 1
fi

echo "romfs-smoke: vfs append/truncate"
echo plus >>$base/b.txt || exit 1
set -- `cat $base/b.txt | wc`
if test "$1" != 2; then
        echo "romfs-smoke: append line-count mismatch" >&2
        exit 1
fi
if test "$2" != 2; then
        echo "romfs-smoke: append word-count mismatch" >&2
        exit 1
fi
if test "$3" != 9; then
        echo "romfs-smoke: append byte-count mismatch" >&2
        exit 1
fi
echo reset >$base/b.txt || exit 1
got=`cat $base/b.txt` || exit 1
if test "$got" != reset; then
        echo "romfs-smoke: truncate mismatch" >&2
        exit 1
fi

echo "romfs-smoke: remount persistence"
/sbin/umount /cart || exit 1
/sbin/mount -t romfs /dev/cartflash0 /cart || exit 1
got=`cat $base/b.txt` || exit 1
if test "$got" != reset; then
        echo "romfs-smoke: remount mismatch" >&2
        exit 1
fi

echo "romfs-smoke: protected system entries"
set -- `romfsctl cat /firmware | wc`
fwbytes=$3
echo bad >/cart/firmware 2>/dev/null
set -- `romfsctl cat /firmware | wc`
if test "$3" != "$fwbytes"; then
        echo "romfs-smoke: firmware changed after vfs write" >&2
        exit 1
fi
if rm /cart/firmware 2>/dev/null; then
        echo "romfs-smoke: firmware remove unexpectedly succeeded" >&2
        exit 1
fi
if romfsctl write /firmware bad >/dev/null 2>&1; then
        echo "romfs-smoke: romfsctl firmware write unexpectedly succeeded" >&2
        exit 1
fi
if romfsctl rm /flashlist >/dev/null 2>&1; then
        echo "romfs-smoke: romfsctl flashlist remove unexpectedly succeeded" >&2
        exit 1
fi
if romfsctl rename /flashmap /flashmap.bad >/dev/null 2>&1; then
        echo "romfs-smoke: romfsctl flashmap rename unexpectedly succeeded" >&2
        exit 1
fi

echo "romfs-smoke: vfs cleanup"
rm $base/b.txt || exit 1
rmdir $base || exit 1

echo "romfs-smoke: romfsctl create/read/rename/delete"
romfsctl rm $ctl/y.txt >/dev/null 2>&1
romfsctl rm $ctl/x.txt >/dev/null 2>&1
romfsctl rmdir $ctl >/dev/null 2>&1
romfsctl mkdir $ctl || exit 1
romfsctl write $ctl/x.txt ctl-data || exit 1
got=`romfsctl cat $ctl/x.txt` || exit 1
if test "$got" != ctl-data; then
        echo "romfs-smoke: romfsctl read mismatch" >&2
        exit 1
fi
romfsctl rename $ctl/x.txt $ctl/y.txt || exit 1
got=`romfsctl cat $ctl/y.txt` || exit 1
if test "$got" != ctl-data; then
        echo "romfs-smoke: romfsctl rename mismatch" >&2
        exit 1
fi
romfsctl rm $ctl/y.txt || exit 1
romfsctl rmdir $ctl || exit 1

echo "romfs-smoke ok"
