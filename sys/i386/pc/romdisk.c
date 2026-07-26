#include "romdisk.h"

#include <sys/param.h>
#include <disk/romdisk.h>

extern unsigned char __i386_romdisk_start[];
extern unsigned char __i386_romdisk_end[];

static const struct romdisk i386_romdisk = {
    __i386_romdisk_start,
    __i386_romdisk_end,
    I386_ROMDISK_ROOT_MINOR,
    DEV_BSHIFT
};

int
i386romdisk_open(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_open(&i386_romdisk, dev, flag, mode);
}

int
i386romdisk_close(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_close(&i386_romdisk, dev, flag, mode);
}

daddr_t
i386romdisk_size(dev_t dev)
{
    return romdisk_bdev_size(&i386_romdisk, dev);
}

void
i386romdisk_strategy(struct buf *bp)
{
    romdisk_bdev_strategy(&i386_romdisk, bp);
}

int
i386romdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return romdisk_bdev_ioctl(&i386_romdisk, dev, cmd, addr, flag);
}
