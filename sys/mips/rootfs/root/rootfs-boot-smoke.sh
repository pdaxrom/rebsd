#!/bin/sh
#
# Exercise the commands that a full interactive rootfs is expected to carry.
#

/root/libc-abi-smoke >/dev/null || exit 1
/root/diskspeed-enospc-smoke.sh >/dev/null || exit 1
PAGER=cat man free >/dev/null || exit 1
PAGER=cat man ifconfig >/dev/null || exit 1
file /bin/* /sbin/* >/dev/null || exit 1
test -x /usr/sbin/ntpdate || exit 1
df -h >/dev/null || exit 1
/bin/date -nu 204001020304.05 >/dev/null || exit 1
case "`/bin/date -u -f %Y`" in
2040)
    ;;
*)
    exit 1
    ;;
esac

echo "rootfs boot smoke: ok"
