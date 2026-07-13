/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common block-disk layer for USB, SD/MMC, IDE and SATA backends. */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/systm.h>
#include <disk/disk.h>

#define DISK_MAX_UNITS                  4u

struct disk_softc {
    unsigned ds_used;
    unsigned ds_unit;
    const struct disk_backend_ops *ds_ops;
    void *ds_arg;
    unsigned ds_sector_count;
    unsigned ds_flags;
    struct disk_mbr ds_mbr;
};

static struct disk_softc disk_softc[DISK_MAX_UNITS];

static void
disk_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static int
disk_present(const struct disk_softc *sc)
{
    return sc->ds_used && (sc->ds_ops->dbo_present == 0 ||
        sc->ds_ops->dbo_present(sc->ds_arg));
}

static int
disk_revalidate(struct disk_softc *sc)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    int error;

    if (!disk_present(sc))
        return ENXIO;
    error = sc->ds_ops->dbo_read(sc->ds_arg, 0, 1, sector);
    if (error != 0)
        return error;
    disk_mbr_parse(&sc->ds_mbr, sector, sc->ds_sector_count);
    return 0;
}

static int
disk_region(struct disk_softc *sc, unsigned part_number, unsigned *start,
    unsigned *sectors)
{
    if (!disk_present(sc))
        return ENXIO;
    if (disk_mbr_region(&sc->ds_mbr, sc->ds_sector_count, part_number,
        start, sectors) != 0)
        return ENXIO;
    return 0;
}

static struct disk_softc *
disk_lookup(unsigned minor_number, unsigned *part_number)
{
    unsigned unit;

    unit = DISK_MINOR_UNIT(minor_number);
    if (unit >= DISK_MAX_UNITS)
        return 0;
    if (part_number != 0)
        *part_number = DISK_MINOR_PART(minor_number);
    if (!disk_present(&disk_softc[unit]))
        return 0;
    return &disk_softc[unit];
}

int
disk_attach(const struct disk_attach_args *args, unsigned *unitp)
{
    struct disk_softc *sc;
    int error;
    unsigned i;

    if (args == 0 || args->da_ops == 0 || args->da_ops->dbo_read == 0 ||
        args->da_sector_size != DISK_SECTOR_SIZE ||
        args->da_sector_count == 0)
        return EINVAL;
    for (i = 0; i < DISK_MAX_UNITS; ++i)
        if (!disk_softc[i].ds_used)
            break;
    if (i == DISK_MAX_UNITS)
        return ENOMEM;

    sc = &disk_softc[i];
    disk_zero(sc, sizeof(*sc));
    sc->ds_used = 1;
    sc->ds_unit = i;
    sc->ds_ops = args->da_ops;
    sc->ds_arg = args->da_arg;
    sc->ds_sector_count = args->da_sector_count;
    sc->ds_flags = args->da_flags;
    error = disk_revalidate(sc);
    if (error != 0) {
        disk_zero(sc, sizeof(*sc));
        return error;
    }

    printf("sd%u: %u 512-byte sectors (%u KB)%s%s\n", sc->ds_unit,
        sc->ds_sector_count, sc->ds_sector_count >> 1,
        (sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ? ", read-only" : "",
        (sc->ds_flags & DISK_FLAG_REMOVABLE) != 0 ? ", removable" : "");
    for (i = 0; i < DISK_MBR_PARTITIONS; ++i)
        if (sc->ds_mbr.dm_partitions[i].dp_type != 0)
            printf("sd%u%c: MBR type=%x start=%u sectors=%u\n",
                sc->ds_unit, 'a' + i,
                sc->ds_mbr.dm_partitions[i].dp_type,
                sc->ds_mbr.dm_partitions[i].dp_offset,
                sc->ds_mbr.dm_partitions[i].dp_nsectors);
    if (unitp != 0)
        *unitp = sc->ds_unit;
    return 0;
}

void
disk_detach(unsigned unit, void *arg)
{
    struct disk_softc *sc;

    if (unit >= DISK_MAX_UNITS)
        return;
    sc = &disk_softc[unit];
    if (!sc->ds_used || sc->ds_arg != arg)
        return;
    disk_zero(sc, sizeof(*sc));
    printf("sd%u: detached\n", unit);
}

void
diskattach(int unit)
{
    (void)unit;
    disk_zero(disk_softc, sizeof(disk_softc));
    printf("disk: block layer ready, MBR partitions\n");
}

int
disk_bdev_open(dev_t dev, int flag, int mode)
{
    struct disk_softc *sc;
    unsigned part_number;
    unsigned start;
    unsigned sectors;

    (void)mode;
    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return ENXIO;
    if ((flag & FWRITE) != 0 &&
        ((sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ||
        sc->ds_ops->dbo_write == 0))
        return EROFS;
    return 0;
}

int
disk_bdev_close(dev_t dev, int flag, int mode)
{
    (void)dev;
    (void)flag;
    (void)mode;
    return 0;
}

static void
disk_bdev_done_error(struct buf *bp, int error)
{
    bp->b_resid = bp->b_bcount;
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
disk_bdev_strategy(struct buf *bp)
{
    struct disk_softc *sc;
    unsigned part_number;
    unsigned start;
    unsigned sectors;
    unsigned block;
    unsigned relative;
    unsigned requested;
    int error;

    sc = disk_lookup(minor(bp->b_dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0) {
        disk_bdev_done_error(bp, ENXIO);
        return;
    }
    if (bp->b_blkno < 0 ||
        (bp->b_bcount & (DISK_SECTOR_SIZE - 1u)) != 0) {
        disk_bdev_done_error(bp, EINVAL);
        return;
    }
    if ((bp->b_flags & B_READ) == 0 &&
        ((sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ||
        sc->ds_ops->dbo_write == 0)) {
        disk_bdev_done_error(bp, EROFS);
        return;
    }

    block = (unsigned)bp->b_blkno;
    if (block > (sectors >> 1)) {
        disk_bdev_done_error(bp, EINVAL);
        return;
    }
    relative = block << 1;
    if (relative >= sectors) {
        if (relative == sectors) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else {
            disk_bdev_done_error(bp, EINVAL);
        }
        return;
    }
    requested = bp->b_bcount / DISK_SECTOR_SIZE;
    if (requested > sectors - relative)
        requested = sectors - relative;
    if (requested == 0) {
        bp->b_resid = bp->b_bcount;
        biodone(bp);
        return;
    }

    if ((bp->b_flags & B_READ) != 0)
        error = sc->ds_ops->dbo_read(sc->ds_arg, start + relative,
            requested, bp->b_addr);
    else
        error = sc->ds_ops->dbo_write(sc->ds_arg, start + relative,
            requested, bp->b_addr);
    if (error != 0) {
        disk_bdev_done_error(bp, error);
        return;
    }
    bp->b_resid = bp->b_bcount - requested * DISK_SECTOR_SIZE;
    biodone(bp);
}

daddr_t
disk_bdev_size(dev_t dev)
{
    struct disk_softc *sc;
    unsigned part_number;
    unsigned start;
    unsigned sectors;

    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return 0;
    return (daddr_t)(sectors >> 1);
}

int
disk_bdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    struct disk_softc *sc;
    const struct disk_partition *part;
    unsigned part_number;
    unsigned start;
    unsigned sectors;
    int error;

    (void)flag;
    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return ENXIO;
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = (int)(sectors >> 1);
        return 0;
    case DIOCREINIT:
        error = disk_revalidate(sc);
        return error;
    case DIOCGETPART:
        if (part_number == DISK_MINOR_WHOLE ||
            part_number > DISK_MBR_PARTITIONS)
            return EINVAL;
        part = &sc->ds_mbr.dm_partitions[part_number - 1u];
        ((struct diskpart *)addr)->dp_status = part->dp_status;
        ((struct diskpart *)addr)->dp_start_chs[0] = 0;
        ((struct diskpart *)addr)->dp_start_chs[1] = 0;
        ((struct diskpart *)addr)->dp_start_chs[2] = 0;
        ((struct diskpart *)addr)->dp_type = part->dp_type;
        ((struct diskpart *)addr)->dp_end_chs[0] = 0;
        ((struct diskpart *)addr)->dp_end_chs[1] = 0;
        ((struct diskpart *)addr)->dp_end_chs[2] = 0;
        ((struct diskpart *)addr)->dp_offset = part->dp_offset;
        ((struct diskpart *)addr)->dp_nsectors = part->dp_nsectors;
        return 0;
    default:
        return EINVAL;
    }
}
