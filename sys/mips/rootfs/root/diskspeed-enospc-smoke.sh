#!/bin/sh
#
# A failed write benchmark must not leave /var full of a partial data file.
#

file=/var/tmp/diskspeed-enospc.$$.data

rm -f "$file"
if diskspeed -m 2 "$file" >/dev/null 2>&1
then
	echo "diskspeed-enospc-smoke: benchmark unexpectedly fitted /var" >&2
	rm -f "$file"
	exit 1
fi
if test -f "$file"
then
	echo "diskspeed-enospc-smoke: partial file was not removed" >&2
	rm -f "$file"
	exit 1
fi

echo "diskspeed-enospc-smoke ok"
