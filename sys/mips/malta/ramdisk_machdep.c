#include <sys/errno.h>
#include <disk/ramdisk.h>
#include <machine/layout.h>
#include <machine/ramdisk.h>

int
mips_ramdisk_register_pools(struct ramdisk_controller *controller)
{
    int error;

    error = ramdisk_controller_register_pool(controller,
        MIPS_RAMDISK_VAR_MINOR,
        MIPS_PHYS_TO_KSEG1(MALTA_RAMDISK_VAR_PHYS_START),
        MALTA_RAMDISK_VAR_BYTES, 0);
    if (error != 0)
        return error;
    return ramdisk_controller_register_pool(controller,
        MIPS_RAMDISK_DATA_MINOR,
        MIPS_PHYS_TO_KSEG1(MALTA_RAMDISK_DATA_PHYS_START),
        MALTA_RAMDISK_DATA_BYTES, 0);
}
