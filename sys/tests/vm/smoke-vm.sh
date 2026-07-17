#!/bin/sh
set -eu

mode=${1:-test}
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
top=$(cd "$script_dir/../../.." && pwd -P)
tmp=${TMPDIR:-/tmp}/rebsd-vm-test.$$
cc=${CC:-cc}

trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

compile()
{
    output=$1
    shift
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" -idirafter "$top/include" \
        "$@" \
        "$top/sys/vm/vm_param.c" "$top/sys/vm/vm_phys.c" \
        "$script_dir/vm_test.c" -o "$output"
}

compile "$tmp/vm_test"

compile_page()
{
    output=$1
    shift
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" -idirafter "$top/include" \
        "$@" \
        "$top/sys/vm/vm_param.c" "$top/sys/vm/vm_phys.c" \
        "$top/sys/vm/vm_page.c" "$script_dir/vm_page_test.c" -o "$output"
}

compile_page "$tmp/vm_page_test"

if compile "$tmp/vm_test_sanitize" -fsanitize=address,undefined \
    -fno-omit-frame-pointer >/dev/null 2>&1; then
    :
else
    rm -f "$tmp/vm_test_sanitize"
fi

if compile_page "$tmp/vm_page_test_sanitize" -fsanitize=address,undefined \
    -fno-omit-frame-pointer >/dev/null 2>&1; then
    :
else
    rm -f "$tmp/vm_page_test_sanitize"
fi

compile_board()
{
    output=$1
    machine_dir=$2
    board_source=$3
    kernel_end=$4
    shift 4
    include_dir="$tmp/include-${output##*/}"
    mkdir -p "$include_dir"
    ln -s "$machine_dir" "$include_dir/machine"
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -DREBSD_VM_BOARD_TEST \
        -DREBSD_VM_TEST_KERNEL_END="$kernel_end" \
        -I "$include_dir" -I "$top/sys" -idirafter "$top/include" \
        "$@" "$top/sys/vm/vm_param.c" "$top/sys/vm/vm_phys.c" \
        "$board_source" "$script_dir/board_map_test.c" -o "$output"
}

compile_board "$tmp/malta_map_test" "$top/sys/mips" \
    "$top/sys/mips/malta/vm_phys_board.c" 0x00180000u \
    -DTEST_MALTA -DMALTA_RAM_SIZE_OVERRIDE=0x04000000u \
    -DMALTA_ROMDISK_BYTES_OVERRIDE=0x02000000u
compile_board "$tmp/ci20_map_test" "$top/sys/mips/ci20" \
    "$top/sys/mips/ci20/vm_phys_board.c" 0x00180000u \
    -DTEST_CI20 -DCI20_RAM_SIZE_OVERRIDE=0x10000000u \
    -DCI20_ROMDISK_BYTES_OVERRIDE=0x02000000u \
    -DCI20_RAMSWAP_BYTES_OVERRIDE=0x02000000u
compile_board "$tmp/n64_map_test" "$top/sys/mips/n64" \
    "$top/sys/mips/n64/vm_phys_board.c" 0x00080000u -DTEST_N64
compile_board "$tmp/n64_debug_map_test" "$top/sys/mips/n64" \
    "$top/sys/mips/n64/vm_phys_board.c" 0x00080000u \
    -DTEST_N64 -DN64_DEBUG_USERMEM_4M
compile_board "$tmp/n64_highres_map_test" "$top/sys/mips/n64" \
    "$top/sys/mips/n64/vm_phys_board.c" 0x00080000u \
    -DTEST_N64 -DN64_HIGHRES_FB

if [ "$mode" = test ]; then
    "$tmp/vm_test"
    "$tmp/vm_page_test"
    if [ -x "$tmp/vm_test_sanitize" ]; then
        "$tmp/vm_test_sanitize"
    fi
    if [ -x "$tmp/vm_page_test_sanitize" ]; then
        "$tmp/vm_page_test_sanitize"
    fi
    "$tmp/malta_map_test"
    "$tmp/ci20_map_test"
    "$tmp/n64_map_test"
    "$tmp/n64_debug_map_test"
    "$tmp/n64_highres_map_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
