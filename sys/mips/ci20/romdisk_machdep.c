#include <sys/param.h>
#include <disk/romdisk.h>
#include <machine/romdisk.h>

extern unsigned char __ci20_romdisk_start[];
extern unsigned char __ci20_romdisk_end[];

static const struct romdisk mips_romdisk = {
    __ci20_romdisk_start,
    __ci20_romdisk_end,
    MIPS_ROMDISK_ROOT_MINOR,
    DEV_BSHIFT,
    0,
    0
};

const struct romdisk *
romdisk_md_device(void)
{
    return &mips_romdisk;
}
