/* Physical ownership for the 32-bit N64 kernel. */

#include <sys/errno.h>
#include <machine/layout.h>
#include <vm/vm_phys.h>

#ifndef REBSD_VM_BOARD_TEST
extern char _end[];
#endif

static int
n64_vm_reserve(struct vm_phys_map *map, vm_paddr_t start, vm_paddr_t end,
    const char *name)
{
    if (start == end)
        return 0;
    if (start > end)
        return EINVAL;
    return vm_phys_map_reserve(map, start, end - start, name);
}

int
vm_phys_board_register(struct vm_phys_map *map, vm_size_t ram_size)
{
    vm_paddr_t kernel_start;
    vm_paddr_t kernel_end;
    vm_paddr_t uarea_start;
    vm_paddr_t user_end;
    vm_paddr_t framebuffer_start;
    vm_paddr_t framebuffer_end;
    vm_paddr_t framebuffer_owned_start;
    vm_paddr_t pool_start;
    vm_paddr_t var_end;
    vm_size_t framebuffer_size;
    vm_size_t var_size;
    int error;

    if (ram_size != N64_RDRAM_SIZE_4M && ram_size != N64_RDRAM_SIZE_8M)
        return EINVAL;
    error = vm_phys_map_add_ram(map, N64_PHYS_RDRAM_BASE, ram_size,
        "rdram");
    if (error != 0)
        return error;

    error = vm_phys_map_reserve(map, 0, VM_PAGE_SIZE, "vectors");
    if (error != 0)
        return error;
    kernel_start = N64_KERNEL_LOAD_VADDR & N64_KSEG_PHYS_MASK;
#ifdef REBSD_VM_BOARD_TEST
    error = vm_paddr_round_page(REBSD_VM_TEST_KERNEL_END, &kernel_end);
#else
    error = vm_paddr_round_page(
        ((vm_vaddr_t)_end) & N64_KSEG_PHYS_MASK, &kernel_end);
#endif
    if (error != 0)
        return error;
    uarea_start = N64_UAREA_VADDR & N64_KSEG_PHYS_MASK;
    if (kernel_end < kernel_start || kernel_end > uarea_start)
        return EINVAL;
    error = n64_vm_reserve(map, kernel_start, kernel_end, "kernel");
    if (error != 0)
        return error;
    error = vm_phys_map_reserve(map, uarea_start,
        N64_UAREA_SIZE, "bootstrap u area");
    if (error != 0)
        return error;

    if (ram_size >= N64_RDRAM_SIZE_8M) {
#ifdef N64_DEBUG_USERMEM_4M
        /* UART-only debug builds lend the framebuffer reserve to RAM disks. */
        framebuffer_start = N64_USER_PHYS_END;
        framebuffer_size = 0;
#else
        framebuffer_start = N64_EXPANSION_FB_PHYS_START;
        framebuffer_size = N64_EXPANSION_FB_RESERVED_BYTES;
#endif
        var_size = N64_RAMDISK_8M_VAR_BYTES;
        user_end = N64_USER_PHYS_END_8M;

        error = n64_vm_reserve(map, N64_USER_PHYS_START,
            N64_STAGE0_PHYS_START, "legacy user window");
        if (error != 0)
            return error;
        error = n64_vm_reserve(map, N64_STAGE0_PHYS_START,
            N64_STAGE0_PHYS_END,
            "legacy user/stage0 alias");
        if (error != 0)
            return error;
        error = n64_vm_reserve(map, N64_STAGE0_PHYS_END, user_end,
            "legacy user window");
        if (error != 0)
            return error;
        framebuffer_owned_start = framebuffer_start;
    } else {
        framebuffer_start = N64_BASE_FB_PHYS_START;
        framebuffer_size = N64_BASE_FB_RESERVED_BYTES;
        var_size = N64_RAMDISK_4M_VAR_BYTES;
        user_end = N64_USER_PHYS_END_4M;

        error = n64_vm_reserve(map, N64_USER_PHYS_START, user_end,
            "legacy user window");
        if (error != 0)
            return error;
        error = n64_vm_reserve(map, N64_STAGE0_PHYS_START,
            framebuffer_start, "stage0/restart");
        if (error != 0)
            return error;
        error = n64_vm_reserve(map, framebuffer_start,
            N64_STAGE0_PHYS_END, "stage0/framebuffer alias");
        if (error != 0)
            return error;
        framebuffer_owned_start = N64_STAGE0_PHYS_END;
    }
    error = vm_paddr_add(framebuffer_start, framebuffer_size,
        &framebuffer_end);
    if (error != 0 || framebuffer_end > ram_size)
        return EINVAL;
    if (framebuffer_owned_start > framebuffer_end)
        return EINVAL;
    error = n64_vm_reserve(map, framebuffer_owned_start, framebuffer_end,
        "framebuffer");
    if (error != 0)
        return error;

    pool_start = framebuffer_end;
    if (var_size >= ram_size - pool_start)
        var_size = 0;
    error = vm_paddr_add(pool_start, var_size, &var_end);
    if (error != 0 || var_end > ram_size)
        return EINVAL;
    error = n64_vm_reserve(map, pool_start, var_end, "/var ramdisk");
    if (error != 0)
        return error;
    return n64_vm_reserve(map, var_end, ram_size, "swap");
}
