/* Physical ownership for Malta, MaltaEL, and the 32-bit Malta64 kernel. */

#include <sys/errno.h>
#include <machine/layout.h>
#include <vm/vm_phys.h>

#ifndef REBSD_VM_BOARD_TEST
extern char _end[];
#endif

static int
malta_vm_reserve_present(struct vm_phys_map *map, vm_size_t ram_size,
    vm_paddr_t start, vm_size_t size, const char *name)
{
    if (size == 0 || start >= ram_size)
        return 0;
    if (size > ram_size - start)
        size = ram_size - start;
    return vm_phys_map_reserve(map, start, size, name);
}

int
vm_phys_board_register(struct vm_phys_map *map, vm_size_t ram_size)
{
    vm_paddr_t kernel_start;
    vm_paddr_t kernel_end;
    vm_paddr_t uarea_start;
    int error;

    if (ram_size != MALTA_RAM_SIZE)
        return EINVAL;
    error = vm_phys_map_add_ram(map, MALTA_PHYS_RAM_BASE, ram_size, "ram");
    if (error != 0)
        return error;

#ifdef MALTA_N64_8M_PROFILE
    error = vm_phys_map_reserve(map, MALTA_PHYS_RAM_BASE, VM_PAGE_SIZE,
        "vectors");
#else
    error = vm_phys_map_reserve(map, MALTA_PHYS_RAM_BASE, MIPS_SIZE_1M,
        "firmware/vectors");
#endif
    if (error != 0)
        return error;

    kernel_start = MIPS_KSEG_TO_PHYS(MALTA_KERNEL_LOAD_VADDR);
#ifdef REBSD_VM_BOARD_TEST
    error = vm_paddr_round_page(REBSD_VM_TEST_KERNEL_END, &kernel_end);
#else
    error = vm_paddr_round_page(MIPS_KSEG_TO_PHYS(_end), &kernel_end);
#endif
    if (error != 0)
        return error;
    uarea_start = MIPS_KSEG_TO_PHYS(MALTA_UAREA_VADDR);
    if (kernel_end < kernel_start || kernel_end > uarea_start)
        return EINVAL;
    if (kernel_end != kernel_start) {
        error = vm_phys_map_reserve(map, kernel_start,
            kernel_end - kernel_start, "kernel");
        if (error != 0)
            return error;
    }

    error = vm_phys_map_reserve(map, uarea_start,
        MALTA_UAREA_SIZE, "bootstrap u area");
    if (error != 0)
        return error;
#ifdef MALTA_N64_8M_PROFILE
    error = vm_phys_map_reserve(map, MALTA_STAGE0_PHYS_START,
        MALTA_STAGE0_BYTES, "stage0/restart");
    if (error != 0)
        return error;
    error = vm_phys_map_reserve(map, MALTA_FRAMEBUFFER_PHYS_START,
        MALTA_FRAMEBUFFER_BYTES, "framebuffer");
    if (error != 0)
        return error;
#else
    error = vm_phys_map_reserve(map, MIPS_USER_PHYS_START,
        MIPS_LEGACY_USER_BYTES, "legacy user window");
    if (error != 0)
        return error;
#endif
    return malta_vm_reserve_present(map, ram_size,
        MALTA_ROMDISK_PHYS_START, MALTA_ROMDISK_BYTES, "rootfs");
}
