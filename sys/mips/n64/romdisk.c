#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/systm.h>
#include <machine/romdisk.h>
#include <machine/rompak.h>

static struct n64_rompak_entry rootfs_entry;
static int rootfs_state;
static int rootfs_reported;

static int
romdisk_locate(void)
{
    if (rootfs_state == 0) {
        rootfs_state = n64_rompak_find("rootfs.img", &rootfs_entry) == 0 ?
            1 : -1;
    }
    if (!rootfs_reported) {
        rootfs_reported = 1;
        if (rootfs_state > 0) {
            printf("n64romdisk: rootfs offset=%x size=%x magic=%x\n",
                rootfs_entry.offset, rootfs_entry.size,
                n64_rompak_read32(rootfs_entry.offset));
        } else {
            printf("n64romdisk: rootfs.img not found in ROM TOC\n");
        }
    }

    return rootfs_state > 0 ? 0 : ENXIO;
}

int
n64romdisk_open(dev_t dev, int flag, int mode)
{
    if (minor(dev) != N64_ROMDISK_ROOT_MINOR)
        return ENXIO;
    if (flag & FWRITE)
        return EROFS;
    return romdisk_locate();
}

int
n64romdisk_close(dev_t dev, int flag, int mode)
{
    return 0;
}

daddr_t
n64romdisk_size(dev_t dev)
{
    if (minor(dev) != N64_ROMDISK_ROOT_MINOR || romdisk_locate() != 0)
        return 0;

    return rootfs_entry.size >> 10;
}

static void
romdisk_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
n64romdisk_strategy(struct buf *bp)
{
    unsigned offset;
    unsigned nbytes;
    int error;

    error = romdisk_locate();
    if (error != 0) {
        romdisk_done_error(bp, error);
        return;
    }
    if (minor(bp->b_dev) != N64_ROMDISK_ROOT_MINOR) {
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
    if (bp->b_blkno > (daddr_t)(rootfs_entry.size >> DEV_BSHIFT)) {
        romdisk_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << DEV_BSHIFT;
    if (offset >= rootfs_entry.size) {
        if (offset == rootfs_entry.size) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else {
            romdisk_done_error(bp, EINVAL);
        }
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > rootfs_entry.size - offset) {
        bp->b_resid = nbytes - (rootfs_entry.size - offset);
        nbytes = rootfs_entry.size - offset;
        bp->b_bcount = nbytes;
    }

    n64_rompak_copy(rootfs_entry.offset + offset, bp->b_addr, nbytes);

    biodone(bp);
}

int
n64romdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = n64romdisk_size(dev);
        return 0;
    default:
        return EINVAL;
    }
}
