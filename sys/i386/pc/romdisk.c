#include "romdisk.h"

#include <sys/param.h>
#include <disk/romdisk.h>

extern unsigned char __i386_romdisk_start[];
extern unsigned char __i386_romdisk_end[];

static const struct romdisk i386_romdisk = {
    __i386_romdisk_start,
    __i386_romdisk_end,
    I386_ROMDISK_ROOT_MINOR,
    DEV_BSHIFT,
    0,
    0
};

const struct romdisk *
romdisk_md_device(void)
{
    return &i386_romdisk;
}
