#!/bin/sh
#
# HTTP downloader smoke.  On Malta, run a host HTTP server on port 8080
# serving a file named wget-smoke.txt, then run this script with no arguments.
# On N64, pass an explicit HTTP URL reachable through usbn0.
#

echo "wget-smoke diag v1"

if test $# -gt 0; then
        url=$1
else
        url=http://10.0.2.2:8080/wget-smoke.txt
fi

cd /tmp || exit 1
rm -f wget-smoke.out

echo "step 1: fetch $url"
wget -q -O wget-smoke.out "$url" || exit 1

echo "step 2: validate body"
grep 'retrobsd wget smoke' wget-smoke.out >/dev/null || {
        cat wget-smoke.out
        exit 1
}

rm -f wget-smoke.out
echo "wget-smoke ok"
