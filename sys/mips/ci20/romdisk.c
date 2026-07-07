#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/systm.h>
#include <machine/romdisk.h>

extern unsigned char __ci20_romdisk_start[];
extern unsigned char __ci20_romdisk_end[];

static unsigned
romdisk_bytes(void)
{
    return __ci20_romdisk_end - __ci20_romdisk_start;
}

int
mipsromdisk_open(dev_t dev, int flag, int mode)
{
    if (minor(dev) != MIPS_ROMDISK_ROOT_MINOR)
        return ENXIO;
    if (flag & FWRITE)
        return EROFS;
    return romdisk_bytes() != 0 ? 0 : ENXIO;
}

int
mipsromdisk_close(dev_t dev, int flag, int mode)
{
    return 0;
}

daddr_t
mipsromdisk_size(dev_t dev)
{
    if (minor(dev) != MIPS_ROMDISK_ROOT_MINOR)
        return 0;
    return romdisk_bytes() >> 10;
}

static void
romdisk_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
mipsromdisk_strategy(struct buf *bp)
{
    unsigned offset;
    unsigned nbytes;
    unsigned size = romdisk_bytes();

    if (minor(bp->b_dev) != MIPS_ROMDISK_ROOT_MINOR) {
        romdisk_done_error(bp, ENXIO);
        return;
    }
    if ((bp->b_flags & B_READ) == 0) {
        romdisk_done_error(bp, EROFS);
        return;
    }
    if (bp->b_blkno < 0) {
        romdisk_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << DEV_BSHIFT;
    if (offset >= size) {
        if (offset == size) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else {
            romdisk_done_error(bp, EINVAL);
        }
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > size - offset) {
        bp->b_resid = nbytes - (size - offset);
        nbytes = size - offset;
        bp->b_bcount = nbytes;
    }

    bcopy(__ci20_romdisk_start + offset, bp->b_addr, nbytes);
    biodone(bp);
}

int
mipsromdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = mipsromdisk_size(dev);
        return 0;
    default:
        return EINVAL;
    }
}
