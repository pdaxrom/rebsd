/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common directly addressable writable-memory block driver. */

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#ifdef DISK_HOST_TEST
void bcopy(const void *, void *, size_t);
#else
#include <sys/systm.h>
#endif

#include <disk/ramdisk.h>

static int
ramdisk_media(const struct ramdisk *ramdisk, unsigned *size)
{
    if (ramdisk == 0 || size == 0)
        return EINVAL;
    if (ramdisk->rd_start == 0 || ramdisk->rd_end <= ramdisk->rd_start)
        return ENXIO;
    *size = (unsigned)(ramdisk->rd_end - ramdisk->rd_start);
    return 0;
}

int
ramdisk_bdev_open(const struct ramdisk *ramdisk, dev_t dev, int flag,
    int mode)
{
    unsigned size;

    (void)flag;
    (void)mode;
    if (ramdisk == 0 || minor(dev) != ramdisk->rd_minor)
        return ENXIO;
    return ramdisk_media(ramdisk, &size);
}

int
ramdisk_bdev_close(const struct ramdisk *ramdisk, dev_t dev, int flag,
    int mode)
{
    (void)ramdisk;
    (void)dev;
    (void)flag;
    (void)mode;
    return 0;
}

daddr_t
ramdisk_bdev_size(const struct ramdisk *ramdisk, dev_t dev)
{
    unsigned size;

    if (ramdisk == 0 || minor(dev) != ramdisk->rd_minor)
        return 0;
    if (ramdisk_media(ramdisk, &size) != 0)
        return 0;
    return size >> ramdisk->rd_block_shift;
}

static void
ramdisk_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
ramdisk_bdev_strategy(const struct ramdisk *ramdisk, struct buf *bp)
{
    unsigned offset;
    unsigned nbytes;
    unsigned size;
    int error;

    error = ramdisk_media(ramdisk, &size);
    if (error != 0) {
        ramdisk_done_error(bp, error);
        return;
    }
    if (minor(bp->b_dev) != ramdisk->rd_minor) {
        ramdisk_done_error(bp, ENXIO);
        return;
    }
    if (bp->b_blkno < 0) {
        ramdisk_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << ramdisk->rd_block_shift;
    if (offset >= size) {
        if (offset == size) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else
            ramdisk_done_error(bp, EINVAL);
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > size - offset) {
        bp->b_resid = nbytes - (size - offset);
        nbytes = size - offset;
        bp->b_bcount = nbytes;
    }

    if (bp->b_flags & B_READ)
        bcopy(ramdisk->rd_start + offset, bp->b_addr, nbytes);
    else
        bcopy(bp->b_addr, ramdisk->rd_start + offset, nbytes);
    biodone(bp);
}

int
ramdisk_bdev_ioctl(const struct ramdisk *ramdisk, dev_t dev, u_int cmd,
    caddr_t addr, int flag)
{
    (void)flag;
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = ramdisk_bdev_size(ramdisk, dev);
        return 0;
    default:
        return EINVAL;
    }
}
