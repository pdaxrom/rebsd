/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common block-disk layer for USB, SD/MMC, IDE and SATA backends. */

#ifdef DISK_HOST_TEST
#include <sys/types.h>
#else
#include <sys/param.h>
#include <sys/conf.h>
#endif
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#ifdef DISK_HOST_TEST
void biodone(struct buf *);
int printf(const char *, ...);
#else
#include <sys/systm.h>
#endif
#include <disk/disk.h>

#define DISK_MAX_UNITS                  4u

struct disk_softc {
    unsigned ds_used;
    unsigned ds_attached;
    /* Reserve a detached unit until every vnode/raw open is closed. */
    unsigned ds_opens;
    unsigned ds_dirty;
    unsigned ds_unit;
    const struct disk_backend_ops *ds_ops;
    void *ds_arg;
    disk_sector_t ds_sector_count;
    unsigned ds_flags;
    struct disk_mbr ds_mbr;
    struct disk_table ds_table;
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

static void
disk_copy(void *to_arg, const void *from_arg, size_t length)
{
    unsigned char *to;
    const unsigned char *from;

    to = (unsigned char *)to_arg;
    from = (const unsigned char *)from_arg;
    while (length-- != 0)
        *to++ = *from++;
}

static int
disk_present(const struct disk_softc *sc)
{
    return sc->ds_used && sc->ds_attached &&
        (sc->ds_ops->dbo_present == 0 ||
        sc->ds_ops->dbo_present(sc->ds_arg));
}

static int
disk_flush(struct disk_softc *sc)
{
    int error;

    if (!sc->ds_dirty)
        return 0;
    if (!disk_present(sc))
        return ENXIO;
    if (sc->ds_ops->dbo_flush == 0) {
        sc->ds_dirty = 0;
        return 0;
    }
    error = sc->ds_ops->dbo_flush(sc->ds_arg);
    if (error == 0)
        sc->ds_dirty = 0;
    return error;
}

static int
disk_gpt_overlap(const struct disk_table *table,
    const struct disk_partition *part, unsigned count)
{
    const struct disk_partition *other;
    disk_sector_t part_last;
    disk_sector_t other_last;
    unsigned i;

    if (part->dp_scheme == DISK_SCHEME_NONE)
        return 0;
    part_last = part->dp_offset + part->dp_nsectors - 1u;
    for (i = 0; i < count; ++i) {
        other = &table->dt_partitions[i];
        if (other->dp_scheme == DISK_SCHEME_NONE)
            continue;
        other_last = other->dp_offset + other->dp_nsectors - 1u;
        if (part->dp_offset <= other_last && other->dp_offset <= part_last)
            return 1;
    }
    return 0;
}

static int
disk_gpt_load(struct disk_softc *sc, disk_sector_t header_lba,
    struct disk_table *table)
{
    struct disk_gpt_header header;
    struct disk_partition part;
    unsigned char sector[DISK_SECTOR_SIZE];
    disk_sector_t array_bytes;
    disk_sector_t array_sector;
    unsigned bytes;
    unsigned crc;
    unsigned entry;
    unsigned offset;
    int error;

    disk_zero(table, sizeof(*table));
    error = sc->ds_ops->dbo_read(sc->ds_arg, header_lba, 1, sector);
    if (error != 0 || disk_gpt_header_parse(&header, sector, header_lba,
        sc->ds_sector_count) != 0)
        return EINVAL;

    array_bytes = (disk_sector_t)header.gh_entry_count *
        header.gh_entry_size;
    crc = disk_crc32_begin();
    entry = 0;
    array_sector = 0;
    while (array_bytes != 0) {
        error = sc->ds_ops->dbo_read(sc->ds_arg,
            header.gh_entries_lba + array_sector, 1, sector);
        if (error != 0)
            return error;
        bytes = array_bytes > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE :
            (unsigned)array_bytes;
        crc = disk_crc32_update(crc, sector, bytes);
        for (offset = 0; offset + header.gh_entry_size <= bytes &&
            entry < header.gh_entry_count;
            offset += header.gh_entry_size, ++entry) {
            if (disk_gpt_entry_parse(&part, sector + offset, &header) != 0)
                return EINVAL;
            if (disk_gpt_overlap(table, &part,
                entry < DISK_PARTITIONS ? entry : DISK_PARTITIONS))
                return EINVAL;
            if (entry < DISK_PARTITIONS)
                table->dt_partitions[entry] = part;
        }
        array_bytes -= bytes;
        ++array_sector;
    }
    if (entry != header.gh_entry_count ||
        disk_crc32_end(crc) != header.gh_entries_crc32)
        return EINVAL;
    table->dt_valid = 1;
    table->dt_scheme = DISK_SCHEME_GPT;
    table->dt_from_backup = header_lba != 1;
    return 0;
}

static int
disk_revalidate(struct disk_softc *sc)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    struct disk_table table;
    int error;

    if (!disk_present(sc))
        return ENXIO;
    error = sc->ds_ops->dbo_read(sc->ds_arg, 0, 1, sector);
    if (error != 0)
        return error;
    disk_mbr_parse(&sc->ds_mbr, sector, sc->ds_sector_count);
    if (disk_mbr_is_protective(&sc->ds_mbr)) {
        error = disk_gpt_load(sc, 1, &table);
        if (error != 0)
            error = disk_gpt_load(sc, sc->ds_sector_count - 1u, &table);
        if (error == 0) {
            sc->ds_table = table;
            return 0;
        }
        disk_zero(&sc->ds_table, sizeof(sc->ds_table));
        printf("sd%u: protective MBR, invalid GPT\n", sc->ds_unit);
        return 0;
    }
    disk_table_from_mbr(&sc->ds_table, &sc->ds_mbr);
    return 0;
}

static int
disk_region(struct disk_softc *sc, unsigned part_number, disk_sector_t *start,
    disk_sector_t *sectors)
{
    if (!disk_present(sc))
        return ENXIO;
    if (disk_table_region(&sc->ds_table, sc->ds_sector_count, part_number,
        start, sectors) != 0)
        return ENXIO;
    return 0;
}

static char *
disk_lba_string(char *buffer, disk_sector_t value)
{
    char reverse[24];
    disk_sector_t quotient;
    unsigned remainder;
    int bit;
    unsigned length;
    unsigned i;

    length = 0;
    do {
        /*
         * Keep the common disk layer independent of compiler 64-bit
         * division helpers.  Some freestanding kernels (notably PCC MIPS)
         * do not get those helpers from a compiler runtime automatically.
         */
        quotient = 0;
        remainder = 0;
        for (bit = 63; bit >= 0; --bit) {
            remainder = (remainder << 1) |
                (unsigned)((value >> bit) & 1u);
            if (remainder >= 10u) {
                remainder -= 10u;
                quotient |= (disk_sector_t)1u << bit;
            }
        }
        reverse[length++] = (char)('0' + remainder);
        value = quotient;
    } while (value != 0);
    for (i = 0; i < length; ++i)
        buffer[i] = reverse[length - i - 1u];
    buffer[length] = 0;
    return buffer;
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
    char sectors_text[24];
    char kbytes_text[24];
    char start_text[24];
    char count_text[24];
    const struct disk_partition *part;
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
    sc->ds_attached = 1;
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

    printf("sd%u: %s 512-byte sectors (%s KB)%s%s\n", sc->ds_unit,
        disk_lba_string(sectors_text, sc->ds_sector_count),
        disk_lba_string(kbytes_text, sc->ds_sector_count >> 1),
        (sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ? ", read-only" : "",
        (sc->ds_flags & DISK_FLAG_REMOVABLE) != 0 ? ", removable" : "");
    if (sc->ds_table.dt_from_backup)
        printf("sd%u: using backup GPT; primary is invalid\n", sc->ds_unit);
    for (i = 0; i < DISK_PARTITIONS; ++i) {
        part = &sc->ds_table.dt_partitions[i];
        if (part->dp_scheme == DISK_SCHEME_MBR)
            printf("sd%u%c: MBR type=%x start=%s sectors=%s\n",
                sc->ds_unit, 'a' + i, part->dp_type,
                disk_lba_string(start_text, part->dp_offset),
                disk_lba_string(count_text, part->dp_nsectors));
        else if (part->dp_scheme == DISK_SCHEME_GPT)
            printf("sd%u%c: GPT entry=%u start=%s sectors=%s\n",
                sc->ds_unit, 'a' + i, i + 1u,
                disk_lba_string(start_text, part->dp_offset),
                disk_lba_string(count_text, part->dp_nsectors));
    }
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
    sc->ds_attached = 0;
    printf("sd%u: detached\n", unit);
    if (sc->ds_opens == 0)
        disk_zero(sc, sizeof(*sc));
}

void
diskattach(int unit)
{
    (void)unit;
    disk_zero(disk_softc, sizeof(disk_softc));
    printf("disk: block layer ready, MBR/GPT partitions, 64-bit LBA\n");
}

int
disk_bdev_open(dev_t dev, int flag, int mode)
{
    struct disk_softc *sc;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;

    (void)mode;
    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return ENXIO;
    if ((flag & FWRITE) != 0 &&
        ((sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ||
        sc->ds_ops->dbo_write == 0))
        return EROFS;
    if (sc->ds_opens == 0xffffffffu)
        return EMFILE;
    ++sc->ds_opens;
    return 0;
}

int
disk_bdev_close(dev_t dev, int flag, int mode)
{
    struct disk_softc *sc;
    int error;
    unsigned unit;

    (void)flag;
    (void)mode;
    unit = DISK_MINOR_UNIT(minor(dev));
    if (unit >= DISK_MAX_UNITS)
        return ENXIO;
    sc = &disk_softc[unit];
    if (!sc->ds_used || sc->ds_opens == 0)
        return ENXIO;
    error = 0;
    if (sc->ds_opens == 1 && sc->ds_attached)
        error = disk_flush(sc);
    --sc->ds_opens;
    if (sc->ds_opens == 0 && !sc->ds_attached)
        disk_zero(sc, sizeof(*sc));
    return error;
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
    disk_sector_t start;
    disk_sector_t sectors;
    disk_sector_t block;
    disk_sector_t relative;
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

    block = (disk_sector_t)bp->b_blkno;
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
    if ((disk_sector_t)requested > sectors - relative)
        requested = (unsigned)(sectors - relative);
    if (requested == 0) {
        bp->b_resid = bp->b_bcount;
        biodone(bp);
        return;
    }

    if ((bp->b_flags & B_READ) != 0)
        error = sc->ds_ops->dbo_read(sc->ds_arg, start + relative,
            requested, bp->b_addr);
    else {
        error = sc->ds_ops->dbo_write(sc->ds_arg, start + relative,
            requested, bp->b_addr);
        if (error == 0)
            sc->ds_dirty = 1;
    }
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
    disk_sector_t start;
    disk_sector_t sectors;

    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return 0;
    if ((sectors >> 1) > 0x7fffffffu)
        return (daddr_t)0x7fffffff;
    return (daddr_t)(sectors >> 1);
}

int
disk_bdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    struct disk_softc *sc;
    const struct disk_partition *part;
    struct diskpart64 *part64;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;
    int error;

    (void)flag;
    sc = disk_lookup(minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return ENXIO;
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        if ((sectors >> 1) > 0x7fffffffu)
            return EOVERFLOW;
        *(int *)addr = (int)(sectors >> 1);
        return 0;
    case DIOCGETSECTORS:
        if (sectors > 0xffffffffu)
            return EOVERFLOW;
        *(unsigned *)addr = (unsigned)sectors;
        return 0;
    case DIOCGETSECTORS64:
        *(disk_sector_t *)addr = sectors;
        return 0;
    case DIOCGETSCHEME:
        *(unsigned *)addr = sc->ds_table.dt_scheme;
        return 0;
    case DIOCREINIT:
        /*
         * Partition mappings must not change under an open vnode or mounted
         * filesystem.  A partition editor holds exactly one whole-disk open;
         * reject partition callers and any additional opens before reading
         * new metadata.
         */
        if (part_number != DISK_MINOR_WHOLE)
            return EINVAL;
        if (sc->ds_opens != 1)
            return EBUSY;
        error = disk_flush(sc);
        if (error != 0)
            return error;
        error = disk_revalidate(sc);
        return error;
    case DIOCFLUSH:
        return disk_flush(sc);
    case DIOCGETPART:
        if (part_number == DISK_MINOR_WHOLE ||
            part_number > DISK_PARTITIONS)
            return EINVAL;
        part = &sc->ds_table.dt_partitions[part_number - 1u];
        if (part->dp_scheme != DISK_SCHEME_MBR)
            return EOPNOTSUPP;
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
    case DIOCGETPART64:
        if (part_number == DISK_MINOR_WHOLE ||
            part_number > DISK_PARTITIONS)
            return EINVAL;
        part = &sc->ds_table.dt_partitions[part_number - 1u];
        if (part->dp_scheme == DISK_SCHEME_NONE)
            return ENXIO;
        part64 = (struct diskpart64 *)addr;
        disk_zero(part64, sizeof(*part64));
        part64->dp_scheme = part->dp_scheme;
        part64->dp_status = part->dp_status;
        part64->dp_type = part->dp_type;
        part64->dp_offset = part->dp_offset;
        part64->dp_nsectors = part->dp_nsectors;
        part64->dp_attributes = part->dp_attributes;
        disk_copy(part64->dp_type_guid, part->dp_type_guid, 16u);
        disk_copy(part64->dp_unique_guid, part->dp_unique_guid, 16u);
        return 0;
    default:
        return EINVAL;
    }
}

#ifndef DISK_HOST_TEST
int
disk_cdev_open(dev_t dev, int flag, int mode)
{
    return disk_bdev_open(dev, flag, mode);
}

int
disk_cdev_close(dev_t dev, int flag, int mode)
{
    return disk_bdev_close(dev, flag, mode);
}

int
disk_cdev_read(dev_t dev, struct uio *uio, int flag)
{
    return rawrw(dev, uio, flag);
}

int
disk_cdev_write(dev_t dev, struct uio *uio, int flag)
{
    return rawrw(dev, uio, flag);
}

int
disk_cdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return disk_bdev_ioctl(dev, cmd, addr, flag);
}
#endif
