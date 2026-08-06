#!/bin/sh
cd /var/tmp || exit 1
rm -f libc-string-smoke

echo "libc-string-smoke diag v1"
echo "step 1: cc string smoke"
cc -o libc-string-smoke /root/libc-string-smoke.c || exit 1
echo "step 2: run string smoke"
./libc-string-smoke || exit 1
rm -f libc-string-smoke
