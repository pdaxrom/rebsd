#!/bin/sh
set -e

topsrc=$1
destination=$2
architecture=$3
board_machine_dir=$4
network_headers=$5
shared_machine_headers=$6
shift 6

if [ -z "$topsrc" ] || [ -z "$destination" ] || [ -z "$architecture" ] ||
    { [ "$network_headers" != "yes" ] && [ "$network_headers" != "no" ]; }; then
    echo "usage: install-user-headers.sh topsrc destination architecture board-machine-dir network-headers:yes|no shared-machine-headers [common-header ...]" >&2
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

mkdir -p "$destination/$architecture"
for header in "$topsrc"/sys/"$architecture"/*.h; do
    if [ -f "$header" ]; then
        cp -p "$header" "$destination/$architecture/"
    fi
done

mkdir -p "$destination/machine"
if [ -n "$board_machine_dir" ] && [ -d "$board_machine_dir" ]; then
    for header in "$board_machine_dir"/*.h; do
        if [ -f "$header" ]; then
            cp -p "$header" "$destination/machine/"
        fi
    done
fi
for header in "$@"; do
    cp -p "$topsrc/sys/$architecture/$header.h" \
        "$destination/machine/$header.h"
done
for header in $shared_machine_headers; do
    cp -p "$topsrc/sys/$architecture/include/machine/$header.h" \
        "$destination/machine/$header.h"
done
