#!/bin/sh
set -e

topsrc=$1
outdir=$2

if [ -z "$topsrc" ] || [ -z "$outdir" ]; then
    echo "usage: mkhostinclude.sh topsrc outdir" >&2
    exit 1
fi

rm -rf "$outdir"
mkdir -p "$outdir/sys"

cp "$topsrc/include/a.out.h" "$outdir/a.out.h"
cp "$topsrc/include/ar.h" "$outdir/ar.h"
cp "$topsrc/include/elf32.h" "$outdir/elf32.h"
cp "$topsrc/include/nlist.h" "$outdir/nlist.h"
cp "$topsrc/include/ranlib.h" "$outdir/ranlib.h"
cp "$topsrc/include/sys/exec_aout.h" "$outdir/sys/exec_aout.h"
