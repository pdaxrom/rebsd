#include <sys/param.h>
#include <disk/romdisk.h>
#include <machine/romdisk.h>

extern unsigned char __malta_romdisk_start[];
extern unsigned char __malta_romdisk_end[];

static const struct romdisk mips_romdisk = {
    __malta_romdisk_start,
    __malta_romdisk_end,
    MIPS_ROMDISK_ROOT_MINOR,
    DEV_BSHIFT
};

int
mipsromdisk_open(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_open(&mips_romdisk, dev, flag, mode);
}

int
mipsromdisk_close(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_close(&mips_romdisk, dev, flag, mode);
}

daddr_t
mipsromdisk_size(dev_t dev)
{
    return romdisk_bdev_size(&mips_romdisk, dev);
}

void
mipsromdisk_strategy(struct buf *bp)
{
    romdisk_bdev_strategy(&mips_romdisk, bp);
}

int
mipsromdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return romdisk_bdev_ioctl(&mips_romdisk, dev, cmd, addr, flag);
}
