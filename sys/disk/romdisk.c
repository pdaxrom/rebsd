/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common byte-backed read-only romdisk block driver. */

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#ifdef DISK_HOST_TEST
void bcopy(const void *, void *, size_t);
#else
#include <sys/systm.h>
#endif

#include <disk/romdisk.h>

static unsigned
romdisk_bytes(const struct romdisk *romdisk)
{
    return (unsigned)(romdisk->rd_end - romdisk->rd_start);
}

int
romdisk_bdev_open(const struct romdisk *romdisk, dev_t dev, int flag, int mode)
{
    (void)mode;
    if (minor(dev) != romdisk->rd_minor)
        return ENXIO;
    if (flag & FWRITE)
        return EROFS;
    return romdisk_bytes(romdisk) != 0 ? 0 : ENXIO;
}

int
romdisk_bdev_close(const struct romdisk *romdisk, dev_t dev, int flag,
    int mode)
{
    (void)romdisk;
    (void)dev;
    (void)flag;
    (void)mode;
    return 0;
}

daddr_t
romdisk_bdev_size(const struct romdisk *romdisk, dev_t dev)
{
    if (minor(dev) != romdisk->rd_minor)
        return 0;
    return romdisk_bytes(romdisk) >> romdisk->rd_block_shift;
}

static void
romdisk_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
romdisk_bdev_strategy(const struct romdisk *romdisk, struct buf *bp)
{
    unsigned offset;
    unsigned nbytes;
    unsigned size;

    size = romdisk_bytes(romdisk);
    if (minor(bp->b_dev) != romdisk->rd_minor) {
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

    offset = (unsigned)bp->b_blkno << romdisk->rd_block_shift;
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

    bcopy(romdisk->rd_start + offset, bp->b_addr, nbytes);
    biodone(bp);
}

int
romdisk_bdev_ioctl(const struct romdisk *romdisk, dev_t dev, u_int cmd,
    caddr_t addr, int flag)
{
    (void)flag;
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = romdisk_bdev_size(romdisk, dev);
        return 0;
    default:
        return EINVAL;
    }
}
