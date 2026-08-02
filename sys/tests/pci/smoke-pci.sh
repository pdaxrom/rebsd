#!/bin/sh
set -eu

mode=${1:-test}
top=$(cd ../../.. && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-pci-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
mkdir -p "$tmp/include/sys"
ln -s "$top/include/sys/disk.h" "$tmp/include/sys/disk.h"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/pci/pci.c" \
    "$top/sys/pci/rtl8169.c" \
    pci_test.c -o "$tmp/pci_test"

"$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
    -DREBSD_DMA_HOST_TEST -I "$tmp/include" \
    -idirafter "$top/include" -idirafter "$top/sys" \
    "$top/sys/kernel/subr_dma.c" \
    "$top/sys/pci/pci.c" \
    "$top/sys/pci/pciide.c" \
    pciide_test.c -o "$tmp/pciide_test"

if [ "$mode" = test ]; then
    "$tmp/pci_test"
"$tmp/pciide_test" pio
"$tmp/pciide_test" dma
"$tmp/pciide_test" piix-dma-pio2
"$tmp/pciide_test" via-dma
"$tmp/pciide_test" via-dma-pio3
"$tmp/pciide_test" via-dma-pio2
"$tmp/pciide_test" auto-fallback
"$tmp/pciide_test" dma-error
"$tmp/pciide_test" dma-timeout
"$tmp/pciide_test" via-dma-timeout
"$tmp/pciide_test" dma-early-irq
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
