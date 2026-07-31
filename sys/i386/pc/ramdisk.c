/* i686 attachment for the common writable-memory block driver. */

#include <sys/param.h>
#include <sys/buf.h>

#include <disk/ramdisk.h>

#include "ramdisk.h"

static unsigned char i386_var_storage[I386_RAMDISK_VAR_BYTES]
    __attribute__((aligned(4096)));

static const struct ramdisk i386_var_ramdisk = {
    i386_var_storage,
    i386_var_storage + sizeof(i386_var_storage),
    I386_RAMDISK_VAR_MINOR,
    DEV_BSHIFT,
};

int
i386_ramdisk_open(dev_t dev, int flag, int mode)
{
    return ramdisk_bdev_open(&i386_var_ramdisk, dev, flag, mode);
}

int
i386_ramdisk_close(dev_t dev, int flag, int mode)
{
    return ramdisk_bdev_close(&i386_var_ramdisk, dev, flag, mode);
}

void
i386_ramdisk_strategy(struct buf *bp)
{
    ramdisk_bdev_strategy(&i386_var_ramdisk, bp);
}

daddr_t
i386_ramdisk_size(dev_t dev)
{
    return ramdisk_bdev_size(&i386_var_ramdisk, dev);
}

int
i386_ramdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return ramdisk_bdev_ioctl(&i386_var_ramdisk, dev, cmd, addr, flag);
}
