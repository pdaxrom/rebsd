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

static int
romdisk_media(const struct romdisk *romdisk, unsigned *size)
{
    if (romdisk == 0 || size == 0)
        return EINVAL;
    if (romdisk->rd_ops != 0 && romdisk->rd_ops->rd_media != 0)
        return romdisk->rd_ops->rd_media(romdisk->rd_cookie, size);
    if (romdisk->rd_start == 0 || romdisk->rd_end <= romdisk->rd_start)
        return ENXIO;
    *size = (unsigned)(romdisk->rd_end - romdisk->rd_start);
    return 0;
}

static int
romdisk_read(const struct romdisk *romdisk, unsigned offset, void *data,
    unsigned nbytes)
{
    if (romdisk->rd_ops != 0 && romdisk->rd_ops->rd_read != 0)
        return romdisk->rd_ops->rd_read(romdisk->rd_cookie, offset, data,
            nbytes);
    bcopy(romdisk->rd_start + offset, data, nbytes);
    return 0;
}

int
romdisk_bdev_open(const struct romdisk *romdisk, dev_t dev, int flag, int mode)
{
    unsigned size;

    (void)mode;
    if (minor(dev) != romdisk->rd_minor)
        return ENXIO;
    if (flag & FWRITE)
        return EROFS;
    return romdisk_media(romdisk, &size);
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
    unsigned size;

    if (minor(dev) != romdisk->rd_minor)
        return 0;
    if (romdisk_media(romdisk, &size) != 0)
        return 0;
    return size >> romdisk->rd_block_shift;
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
    int error;

    error = romdisk_media(romdisk, &size);
    if (error != 0) {
        romdisk_done_error(bp, error);
        return;
    }
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

    error = romdisk_read(romdisk, offset, bp->b_addr, nbytes);
    if (error != 0) {
        romdisk_done_error(bp, error);
        return;
    }
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

#if defined(KERNEL) && !defined(DISK_HOST_TEST)
int
romdisk_open(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_open(romdisk_md_device(), dev, flag, mode);
}

int
romdisk_close(dev_t dev, int flag, int mode)
{
    return romdisk_bdev_close(romdisk_md_device(), dev, flag, mode);
}

daddr_t
romdisk_size(dev_t dev)
{
    return romdisk_bdev_size(romdisk_md_device(), dev);
}

void
romdisk_strategy(struct buf *bp)
{
    romdisk_bdev_strategy(romdisk_md_device(), bp);
}

int
romdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return romdisk_bdev_ioctl(romdisk_md_device(), dev, cmd, addr, flag);
}
#endif
