#include <sys/errno.h>
#include <disk/ramdisk.h>
#include <machine/n64.h>
#include <machine/ramdisk.h>

int
mips_ramdisk_register_pools(struct ramdisk_controller *controller)
{
    unsigned memsize;
    unsigned pool_base;
    unsigned pool_bytes;
    unsigned var_bytes;
    int error;

    memsize = n64_rdram_size();
    if (memsize >= N64_RDRAM_SIZE_8M) {
#ifdef N64_DEBUG_USERMEM_4M
        pool_base = N64_USER_PHYS_END;
#else
        pool_base = N64_EXPANSION_RAMDISK_DATA_PHYS_START;
#endif
        pool_bytes = memsize - pool_base;
        var_bytes = N64_RAMDISK_8M_VAR_BYTES;
    } else {
        pool_base = N64_BASE_RAMDISK_DATA_PHYS_START;
        pool_bytes = N64_BASE_RAMDISK_DATA_BYTES;
        var_bytes = N64_RAMDISK_4M_VAR_BYTES;
    }
    if (var_bytes == 0 || var_bytes >= pool_bytes)
        return ENOSPC;

    error = ramdisk_controller_register_pool(controller,
        MIPS_RAMDISK_VAR_MINOR, N64_PHYS_TO_KSEG1(pool_base),
        var_bytes, 0);
    if (error != 0)
        return error;
    error = ramdisk_controller_register_pool(controller,
        MIPS_RAMDISK_DATA_MINOR,
        N64_PHYS_TO_KSEG1(pool_base + var_bytes),
        pool_bytes - var_bytes, 0);
    return error;
}
