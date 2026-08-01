#!/bin/sh
set -eu

top=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
tmp=${TMPDIR:-/tmp}/rebsd-vtconsole-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cc -std=c99 -Wall -Wextra -Werror -I "$top/sys" \
    "$top/sys/console/vtconsole.c" vtconsole_test.c \
    -o "$tmp/vtconsole_test"

if [ "${1:-test}" = test ]; then
    "$tmp/vtconsole_test"
fi
