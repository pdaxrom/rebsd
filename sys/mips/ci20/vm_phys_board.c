/* Physical ownership for the 32-bit Ci20 kernel. */

#include <sys/errno.h>
#include <machine/layout.h>
#include <vm/vm_phys.h>

#ifndef REBSD_VM_BOARD_TEST
extern char _end[];
#endif

static int
ci20_vm_reserve_low_present(struct vm_phys_map *map, vm_size_t low_ram_size,
    vm_paddr_t start, vm_size_t size, const char *name)
{
    if (size == 0 || start >= low_ram_size)
        return 0;
    if (size > low_ram_size - start)
        size = low_ram_size - start;
    return vm_phys_map_reserve(map, start, size, name);
}

int
vm_phys_board_register(struct vm_phys_map *map, vm_size_t ram_size)
{
    vm_paddr_t kernel_start;
    vm_paddr_t kernel_end;
    vm_paddr_t uarea_start;
    vm_size_t low_ram_size;
    int error;

    if (ram_size != CI20_RAM_SIZE || ram_size < CI20_LOW_RAM_BYTES ||
        ram_size > CI20_LOW_RAM_BYTES + CI20_HIGH_RAM_MAX_BYTES)
        return EINVAL;
    low_ram_size = CI20_LOW_RAM_BYTES;
    error = vm_phys_map_add_ram(map, CI20_PHYS_RAM_BASE, low_ram_size,
        "low ram");
    if (error != 0)
        return error;
    if (CI20_HIGH_RAM_BYTES != 0) {
        error = vm_phys_map_add_ram(map, CI20_HIGH_RAM_PHYS_START,
            CI20_HIGH_RAM_BYTES, "high ram");
        if (error != 0)
            return error;
    }
    error = vm_phys_map_reserve(map, CI20_PHYS_RAM_BASE, CI20_SIZE_1M,
        "vectors/low RAM");
    if (error != 0)
        return error;

    kernel_start = MIPS_KSEG_TO_PHYS(CI20_KERNEL_LOAD_VADDR);
#ifdef REBSD_VM_BOARD_TEST
    error = vm_paddr_round_page(REBSD_VM_TEST_KERNEL_END, &kernel_end);
#else
    error = vm_paddr_round_page(MIPS_KSEG_TO_PHYS(_end), &kernel_end);
#endif
    if (error != 0)
        return error;
    uarea_start = MIPS_KSEG_TO_PHYS(CI20_UAREA_VADDR);
    if (kernel_end < kernel_start || kernel_end > uarea_start)
        return EINVAL;
    if (kernel_end != kernel_start) {
        error = vm_phys_map_reserve(map, kernel_start,
            kernel_end - kernel_start, "kernel");
        if (error != 0)
            return error;
    }

    error = vm_phys_map_reserve(map, uarea_start,
        CI20_UAREA_SIZE, "bootstrap u area");
    if (error != 0)
        return error;
    error = vm_phys_map_reserve(map, MIPS_USER_PHYS_START,
        MIPS_LEGACY_USER_BYTES, "legacy user window");
    if (error != 0)
        return error;
    error = ci20_vm_reserve_low_present(map, low_ram_size,
        CI20_ROMDISK_PHYS_START, CI20_ROMDISK_BYTES, "rootfs");
    if (error != 0)
        return error;
    return ci20_vm_reserve_low_present(map, low_ram_size,
        CI20_FRAMEBUFFER_PHYS_START, CI20_FRAMEBUFFER_BYTES,
        "framebuffer");
}
