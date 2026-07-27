#!/bin/sh
set -e

topsrc=$1
destination=$2
board_machine_dir=$3
network_headers=$4
shared_machine_headers=$5
shift 5

if [ -z "$topsrc" ] || [ -z "$destination" ] ||
    { [ "$network_headers" != "yes" ] && [ "$network_headers" != "no" ]; }; then
    echo "usage: install-user-headers.sh topsrc destination board-machine-dir network-headers:yes|no shared-machine-headers [common-header ...]" >&2
    exit 1
fi

rm -rf "$destination"
mkdir -p "$destination"
cp -pR "$topsrc/include/." "$destination/"

rm -rf "$destination/machine" "$destination/sys"
cp -pR "$topsrc/sys/include" "$destination/sys"

if [ "$network_headers" = "yes" ]; then
    mkdir -p "$destination/net"
    cp -p "$topsrc"/sys/net/*.h "$destination/net/"

    mkdir -p "$destination/netinet"
    for header in "$topsrc"/sys/netinet/*.h; do
        if [ "${header##*/}" != "in.h" ]; then
            cp -p "$header" "$destination/netinet/"
        fi
    done
fi

mkdir -p "$destination/mips"
cp -p "$topsrc"/sys/mips/*.h "$destination/mips/"

mkdir -p "$destination/machine"
if [ -n "$board_machine_dir" ] && [ -d "$board_machine_dir" ]; then
    cp -p "$board_machine_dir"/*.h "$destination/machine/"
fi
for header in "$@"; do
    cp -p "$topsrc/sys/mips/$header.h" \
        "$destination/machine/$header.h"
done
for header in $shared_machine_headers; do
    cp -p "$topsrc/sys/mips/include/machine/$header.h" \
        "$destination/machine/$header.h"
done
