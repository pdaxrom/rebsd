/* Shared MIPS attachment for the common RAM block-device controller. */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <disk/ramdisk.h>
#include <machine/ramdisk.h>

int mips_ramdisk_register_pools(struct ramdisk_controller *);

static struct ramdisk_controller mips_ramdisks;
static int mips_ramdisks_initialized;

static int
mips_ramdisk_attach(void)
{
    int error;

    if (mips_ramdisks_initialized)
        return 0;
    ramdisk_controller_init(&mips_ramdisks);
    error = mips_ramdisk_register_pools(&mips_ramdisks);
    if (error != 0)
        return error;
    mips_ramdisks_initialized = 1;
    return 0;
}

int
mips_ramdisk_open(dev_t dev, int flag, int mode)
{
    if (mips_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_open(&mips_ramdisks, dev, flag, mode);
}

int
mips_ramdisk_close(dev_t dev, int flag, int mode)
{
    if (mips_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_close(&mips_ramdisks, dev, flag, mode);
}

void
mips_ramdisk_strategy(struct buf *bp)
{
    if (mips_ramdisk_attach() == 0) {
        ramdisk_controller_strategy(&mips_ramdisks, bp);
        return;
    }
    bp->b_error = ENXIO;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

daddr_t
mips_ramdisk_size(dev_t dev)
{
    if (mips_ramdisk_attach() != 0)
        return 0;
    return ramdisk_controller_size(&mips_ramdisks, dev);
}

int
mips_ramdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    if (mips_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_ioctl(&mips_ramdisks, dev, cmd, addr,
        flag);
}

int
mips_ramdisk_compression_stats(int minor_number,
    struct ramcomp_stats *stats)
{
    if (mips_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_compression_stats(&mips_ramdisks,
        minor_number, stats);
}
