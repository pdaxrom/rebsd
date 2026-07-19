#!/bin/sh -
#
#	@(#)usermem.sh	5.4 (Berkeley) 9/17/85
#
: Query the supported kernel interface instead of reading /dev/kmem.
SIZE=`sysctl -n hw.usermem 2>/dev/null`

if test 0$SIZE -le 0
then
    echo 0;exit 1
else
    echo $SIZE
fi
