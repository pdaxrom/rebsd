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

compile_map()
{
    output=$1
    shift
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" -idirafter "$top/include" \
        "$@" "$top/sys/vm/vm_param.c" "$top/sys/vm/vm_map.c" \
        "$script_dir/vm_map_test.c" -o "$output"
}

compile_map "$tmp/vm_map_test"

compile_pmap()
{
    output=$1
    shift
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" -idirafter "$top/include" \
        "$@" \
        "$top/sys/vm/vm_param.c" "$top/sys/vm/vm_phys.c" \
        "$top/sys/vm/vm_page.c" "$top/sys/vm/vm_map.c" \
        "$top/sys/vm/vm_object.c" "$top/sys/vm/vm_shm.c" \
        "$top/sys/vm/vm_sysv_shm.c" \
        "$top/sys/vm/vmspace.c" "$top/sys/vm/vmspace_access.c" \
        "$top/sys/vm/vmspace_fault.c" \
        "$top/sys/vm/vmspace_fault_api.c" \
        "$top/sys/vm/vmspace_fault_validate.c" \
        "$top/sys/mips/common/pmap.c" \
        "$script_dir/pmap_test.c" -o "$output"
}

compile_pmap "$tmp/pmap_test"

compile_zswap()
{
    output=$1
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" \
        -idirafter "$top/include" \
        "$top/sys/vm/zswap.c" "$script_dir/zswap_test.c" \
        -o "$output"
}

compile_zswap "$tmp/zswap_test"

compile_linux_swap()
{
    output=$1
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -DREBSD_VM_HOST_TEST -I "$top/sys" -idirafter "$top/include" \
        "$top/sys/vm/swap_linux.c" \
        "$script_dir/swap_linux_test.c" -o "$output"
}

compile_linux_swap "$tmp/swap_linux_test"

compile_rmap()
{
    output=$1
    shift
    "$cc" -std=c99 -Wall -Wextra -Werror -pedantic \
        -fno-builtin-malloc -DKERNEL -DREBSD_RMAP_HOST_TEST \
        -I "$top/sys" -idirafter "$top/include" \
        "$@" "$top/sys/kernel/subr_rmap.c" \
        "$script_dir/rmap_test.c" -o "$output"
}

compile_rmap "$tmp/rmap_test"

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

if compile_map "$tmp/vm_map_test_sanitize" -fsanitize=address,undefined \
    -fno-omit-frame-pointer >/dev/null 2>&1; then
    :
else
    rm -f "$tmp/vm_map_test_sanitize"
fi

if compile_pmap "$tmp/pmap_test_sanitize" -fsanitize=address,undefined \
    -fno-omit-frame-pointer >/dev/null 2>&1; then
    :
else
    rm -f "$tmp/pmap_test_sanitize"
fi

if compile_rmap "$tmp/rmap_test_sanitize" \
    -fsanitize=address,undefined -fno-omit-frame-pointer >/dev/null 2>&1; then
    :
else
    rm -f "$tmp/rmap_test_sanitize"
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
compile_board "$tmp/malta_n64_8m_map_test" "$top/sys/mips" \
    "$top/sys/mips/malta/vm_phys_board.c" 0x00080000u \
    -DTEST_MALTA_N64_8M -DMALTA_N64_8M_PROFILE \
    -DMALTA_RAM_SIZE_OVERRIDE=0x00800000u \
    -DMALTA_ROMDISK_BYTES_OVERRIDE=0x02000000u \
    -DMALTA_RAMSWAP_BYTES_OVERRIDE=0x001c0000u
compile_board "$tmp/ci20_map_test" "$top/sys/mips/ci20" \
    "$top/sys/mips/ci20/vm_phys_board.c" 0x00180000u \
    -DTEST_CI20 -DCI20_RAM_SIZE_OVERRIDE=0x10000000u \
    -DCI20_ROMDISK_BYTES_OVERRIDE=0x02000000u \
    -DCI20_RAMSWAP_BYTES_OVERRIDE=0x02000000u
compile_board "$tmp/ci20_1g_map_test" "$top/sys/mips/ci20" \
    "$top/sys/mips/ci20/vm_phys_board.c" 0x00180000u \
    -DTEST_CI20_1G -DCI20_RAM_SIZE_OVERRIDE=0x40000000u \
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
    "$tmp/vm_map_test"
    "$tmp/pmap_test"
    "$tmp/zswap_test"
    "$tmp/swap_linux_test"
    "$tmp/rmap_test"
    if [ -x "$tmp/vm_test_sanitize" ]; then
        "$tmp/vm_test_sanitize"
    fi
    if [ -x "$tmp/vm_page_test_sanitize" ]; then
        "$tmp/vm_page_test_sanitize"
    fi
    if [ -x "$tmp/vm_map_test_sanitize" ]; then
        "$tmp/vm_map_test_sanitize"
    fi
    if [ -x "$tmp/pmap_test_sanitize" ]; then
        "$tmp/pmap_test_sanitize"
    fi
    if [ -x "$tmp/rmap_test_sanitize" ]; then
        "$tmp/rmap_test_sanitize"
    fi
    "$tmp/malta_map_test"
    "$tmp/malta_n64_8m_map_test"
    "$tmp/ci20_map_test"
    "$tmp/ci20_1g_map_test"
    "$tmp/n64_map_test"
    "$tmp/n64_debug_map_test"
    "$tmp/n64_highres_map_test"
elif [ "$mode" != compile ]; then
    echo "usage: $0 [compile|test]" >&2
    exit 2
fi
