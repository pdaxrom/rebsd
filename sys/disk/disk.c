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
#include <sys/uio.h>
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

#ifndef DISK_MAX_UNITS
#define DISK_MAX_UNITS                  4u
#endif
#ifndef DISK_READ_CACHE_SLOTS
#define DISK_READ_CACHE_SLOTS           2u
#endif
#ifndef DISK_READ_AHEAD_MAX_SECTORS
#define DISK_READ_AHEAD_MAX_SECTORS     256u
#endif
#ifndef DISK_WRITE_CACHE_SLOTS
#define DISK_WRITE_CACHE_SLOTS          3u
#endif
#ifndef DISK_WRITE_BACK_MAX_SECTORS
#define DISK_WRITE_BACK_MAX_SECTORS     256u
#endif
#define DISK_WRITE_DIRTY_WORD_BITS      32u
#define DISK_WRITE_DIRTY_WORDS          \
    (DISK_WRITE_BACK_MAX_SECTORS / DISK_WRITE_DIRTY_WORD_BITS)

struct disk_softc {
    unsigned ds_used;
    unsigned ds_attached;
    /* Reserve a detached unit until every vnode/raw open is closed. */
    unsigned ds_opens;
    unsigned ds_dirty;
    /* ds_unit is the internal attachment handle; class_unit is /dev unit. */
    unsigned ds_unit;
    unsigned ds_class;
    unsigned ds_class_unit;
    const struct disk_backend_ops *ds_ops;
    void *ds_arg;
    disk_sector_t ds_sector_count;
    unsigned ds_flags;
    unsigned ds_read_ahead_sectors;
    unsigned ds_write_back_sectors;
    unsigned ds_cache_epoch;
    struct disk_mbr ds_mbr;
    struct disk_table ds_table;
};

struct disk_read_cache {
    struct disk_softc *dc_owner;
    disk_sector_t dc_start;
    unsigned dc_count;
    unsigned dc_stamp;
    unsigned dc_epoch;
    unsigned dc_busy;
    unsigned char dc_data[DISK_READ_AHEAD_MAX_SECTORS * DISK_SECTOR_SIZE];
};

struct disk_write_cache {
    struct disk_softc *dc_owner;
    disk_sector_t dc_start;
    unsigned dc_count;
    unsigned dc_stamp;
    unsigned dc_dirty_stamp;
    unsigned dc_dirty_count;
    unsigned dc_dirty[DISK_WRITE_DIRTY_WORDS];
    unsigned char dc_data[DISK_WRITE_BACK_MAX_SECTORS * DISK_SECTOR_SIZE];
};

static struct disk_softc disk_softc[DISK_MAX_UNITS];
static struct disk_read_cache disk_read_cache[DISK_READ_CACHE_SLOTS];
static struct disk_write_cache disk_write_cache[DISK_WRITE_CACHE_SLOTS];
static unsigned disk_read_cache_clock;

static int disk_present(const struct disk_softc *);

static const char *
disk_class_prefix(unsigned disk_class)
{
    if (disk_class == DISK_CLASS_WD)
        return "wd";
    return "sd";
}

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

static unsigned
disk_cache_stamp(void)
{
    ++disk_read_cache_clock;
    if (disk_read_cache_clock == 0)
        ++disk_read_cache_clock;
    return disk_read_cache_clock;
}

static void
disk_cache_invalidate(struct disk_softc *sc)
{
    struct disk_read_cache *cache;
    unsigned i;

    ++sc->ds_cache_epoch;
    for (i = 0; i < DISK_READ_CACHE_SLOTS; ++i) {
        cache = &disk_read_cache[i];
        if (cache->dc_owner == sc && !cache->dc_busy) {
            cache->dc_owner = 0;
            cache->dc_count = 0;
        }
    }
}

static int
disk_write_cache_is_dirty(const struct disk_write_cache *cache,
    unsigned sector)
{
    return (cache->dc_dirty[sector / DISK_WRITE_DIRTY_WORD_BITS] &
        (1u << (sector & (DISK_WRITE_DIRTY_WORD_BITS - 1u)))) != 0;
}

static void
disk_write_cache_mark_dirty(struct disk_write_cache *cache, unsigned sector)
{
    unsigned *word;
    unsigned mask;

    word = &cache->dc_dirty[sector / DISK_WRITE_DIRTY_WORD_BITS];
    mask = 1u << (sector & (DISK_WRITE_DIRTY_WORD_BITS - 1u));
    if ((*word & mask) == 0) {
        *word |= mask;
        ++cache->dc_dirty_count;
    }
}

static void
disk_write_cache_clear_dirty(struct disk_write_cache *cache,
    unsigned sector)
{
    unsigned *word;
    unsigned mask;

    word = &cache->dc_dirty[sector / DISK_WRITE_DIRTY_WORD_BITS];
    mask = 1u << (sector & (DISK_WRITE_DIRTY_WORD_BITS - 1u));
    if ((*word & mask) != 0) {
        *word &= ~mask;
        --cache->dc_dirty_count;
    }
}

static int
disk_write_cache_flush_slot(struct disk_write_cache *cache)
{
    struct disk_softc *sc;
    unsigned first;
    unsigned count;
    unsigned i;
    int error;

    if (cache == 0 || cache->dc_dirty_count == 0)
        return 0;
    sc = cache->dc_owner;
    if (sc == 0 || !disk_present(sc))
        return ENXIO;
    i = 0;
    while (i < cache->dc_count) {
        while (i < cache->dc_count &&
            !disk_write_cache_is_dirty(cache, i))
            ++i;
        if (i == cache->dc_count)
            break;
        first = i;
        while (i < cache->dc_count &&
            disk_write_cache_is_dirty(cache, i))
            ++i;
        count = i - first;
        error = sc->ds_ops->dbo_write(sc->ds_arg,
            cache->dc_start + first, count,
            cache->dc_data + first * DISK_SECTOR_SIZE);
        if (error != 0)
            return error;
        while (count-- != 0)
            disk_write_cache_clear_dirty(cache, first++);
    }
    cache->dc_dirty_stamp = 0;
    return 0;
}

static int
disk_write_cache_flush(struct disk_softc *sc)
{
    struct disk_write_cache *cache;
    struct disk_write_cache *oldest;
    unsigned i;
    int error;

    for (;;) {
        oldest = 0;
        for (i = 0; i < DISK_WRITE_CACHE_SLOTS; ++i) {
            cache = &disk_write_cache[i];
            if (cache->dc_owner != sc || cache->dc_dirty_count == 0)
                continue;
            if (oldest == 0 ||
                cache->dc_dirty_stamp < oldest->dc_dirty_stamp)
                oldest = cache;
        }
        if (oldest == 0)
            return 0;
        error = disk_write_cache_flush_slot(oldest);
        if (error != 0)
            return error;
    }
}

static void
disk_write_cache_discard(struct disk_softc *sc)
{
    struct disk_write_cache *cache;
    unsigned i;

    for (i = 0; i < DISK_WRITE_CACHE_SLOTS; ++i) {
        cache = &disk_write_cache[i];
        if (cache->dc_owner == sc)
            disk_zero(cache, sizeof(*cache));
    }
}

static struct disk_write_cache *
disk_write_cache_get(struct disk_softc *sc, disk_sector_t absolute,
    int *errorp)
{
    struct disk_write_cache *cache;
    struct disk_write_cache *victim;
    disk_sector_t start;
    unsigned count;
    unsigned i;
    int error;

    count = sc->ds_write_back_sectors;
    start = absolute & ~((disk_sector_t)count - 1u);
    for (i = 0; i < DISK_WRITE_CACHE_SLOTS; ++i) {
        cache = &disk_write_cache[i];
        if (cache->dc_owner == sc && cache->dc_start == start) {
            cache->dc_stamp = disk_cache_stamp();
            *errorp = 0;
            return cache;
        }
    }

    victim = 0;
    for (i = 0; i < DISK_WRITE_CACHE_SLOTS; ++i) {
        cache = &disk_write_cache[i];
        if (cache->dc_owner == 0) {
            victim = cache;
            break;
        }
        if (cache->dc_dirty_count == 0) {
            if (victim == 0 || victim->dc_dirty_count != 0 ||
                cache->dc_stamp < victim->dc_stamp)
                victim = cache;
        } else if (victim == 0 || (victim->dc_dirty_count != 0 &&
            cache->dc_stamp < victim->dc_stamp)) {
            victim = cache;
        }
    }
    if (victim == 0) {
        *errorp = EBUSY;
        return 0;
    }
    error = disk_write_cache_flush_slot(victim);
    if (error != 0) {
        *errorp = error;
        return 0;
    }
    disk_zero(victim, sizeof(*victim));
    victim->dc_owner = sc;
    victim->dc_start = start;
    victim->dc_count = count;
    if ((disk_sector_t)count > sc->ds_sector_count - start)
        victim->dc_count = (unsigned)(sc->ds_sector_count - start);
    victim->dc_stamp = disk_cache_stamp();
    *errorp = 0;
    return victim;
}

static int
disk_write_cached(struct disk_softc *sc, disk_sector_t absolute,
    unsigned count, const void *vdata)
{
    struct disk_write_cache *cache;
    const unsigned char *data;
    unsigned chunk;
    unsigned i;
    unsigned offset;
    int error;

    data = (const unsigned char *)vdata;
    while (count != 0) {
        cache = disk_write_cache_get(sc, absolute, &error);
        if (cache == 0)
            return error;
        offset = (unsigned)(absolute - cache->dc_start);
        chunk = cache->dc_count - offset;
        if (chunk > count)
            chunk = count;
        disk_copy(cache->dc_data + offset * DISK_SECTOR_SIZE, data,
            chunk * DISK_SECTOR_SIZE);
        if (cache->dc_dirty_count == 0)
            cache->dc_dirty_stamp = disk_cache_stamp();
        for (i = 0; i < chunk; ++i)
            disk_write_cache_mark_dirty(cache, offset + i);
        cache->dc_stamp = disk_cache_stamp();
        sc->ds_dirty = 1;
        absolute += chunk;
        count -= chunk;
        data += chunk * DISK_SECTOR_SIZE;
    }
    return 0;
}

static void
disk_write_cache_overlay(struct disk_softc *sc, disk_sector_t absolute,
    unsigned count, void *vdata)
{
    struct disk_write_cache *cache;
    unsigned char *data;
    disk_sector_t first;
    disk_sector_t last;
    disk_sector_t request_last;
    disk_sector_t sector;
    unsigned cache_offset;
    unsigned data_offset;
    unsigned i;

    data = (unsigned char *)vdata;
    request_last = absolute + count;
    for (i = 0; i < DISK_WRITE_CACHE_SLOTS; ++i) {
        cache = &disk_write_cache[i];
        if (cache->dc_owner != sc || cache->dc_dirty_count == 0)
            continue;
        first = absolute > cache->dc_start ? absolute : cache->dc_start;
        last = request_last < cache->dc_start + cache->dc_count ?
            request_last : cache->dc_start + cache->dc_count;
        for (sector = first; sector < last; ++sector) {
            cache_offset = (unsigned)(sector - cache->dc_start);
            if (!disk_write_cache_is_dirty(cache, cache_offset))
                continue;
            data_offset = (unsigned)(sector - absolute);
            disk_copy(data + data_offset * DISK_SECTOR_SIZE,
                cache->dc_data + cache_offset * DISK_SECTOR_SIZE,
                DISK_SECTOR_SIZE);
        }
    }
}

static int
disk_backend_read_overlay(struct disk_softc *sc, disk_sector_t absolute,
    unsigned count, void *data)
{
    int error;

    error = sc->ds_ops->dbo_read(sc->ds_arg, absolute, count, data);
    if (error == 0)
        disk_write_cache_overlay(sc, absolute, count, data);
    return error;
}

static int
disk_cached_read(struct disk_softc *sc, disk_sector_t region_start,
    disk_sector_t region_sectors, disk_sector_t relative, unsigned requested,
    void *data)
{
    struct disk_read_cache *cache;
    struct disk_read_cache *victim;
    disk_sector_t absolute;
    disk_sector_t window_relative;
    unsigned count;
    unsigned epoch;
    unsigned i;
    unsigned offset;
    int error;

    if (sc->ds_read_ahead_sectors <= requested ||
        requested > DISK_READ_AHEAD_MAX_SECTORS)
        return disk_backend_read_overlay(sc, region_start + relative,
            requested, data);
    absolute = region_start + relative;
    for (i = 0; i < DISK_READ_CACHE_SLOTS; ++i) {
        cache = &disk_read_cache[i];
        if (cache->dc_busy || cache->dc_owner != sc ||
            absolute < cache->dc_start)
            continue;
        offset = (unsigned)(absolute - cache->dc_start);
        if (offset <= cache->dc_count &&
            requested <= cache->dc_count - offset) {
            disk_copy(data, cache->dc_data + offset * DISK_SECTOR_SIZE,
                requested * DISK_SECTOR_SIZE);
            disk_write_cache_overlay(sc, absolute, requested, data);
            cache->dc_stamp = disk_cache_stamp();
            return 0;
        }
    }

    victim = 0;
    for (i = 0; i < DISK_READ_CACHE_SLOTS; ++i) {
        cache = &disk_read_cache[i];
        if (cache->dc_busy)
            continue;
        if (cache->dc_owner == 0) {
            victim = cache;
            break;
        }
        if (victim == 0 || cache->dc_stamp < victim->dc_stamp)
            victim = cache;
    }
    if (victim == 0)
        return disk_backend_read_overlay(sc, absolute, requested, data);

    count = sc->ds_read_ahead_sectors;
    if (count > DISK_READ_AHEAD_MAX_SECTORS)
        count = DISK_READ_AHEAD_MAX_SECTORS;
    /* Read-ahead windows are powers of two; avoid 64-bit division helpers. */
    window_relative = relative & ~((disk_sector_t)count - 1u);
    if (relative - window_relative > count - requested)
        window_relative = relative;
    if ((disk_sector_t)count > region_sectors - window_relative)
        count = (unsigned)(region_sectors - window_relative);
    if (count < requested)
        return disk_backend_read_overlay(sc, absolute, requested, data);

    epoch = sc->ds_cache_epoch;
    victim->dc_owner = sc;
    victim->dc_count = 0;
    victim->dc_busy = 1;
    error = sc->ds_ops->dbo_read(sc->ds_arg,
        region_start + window_relative, count, victim->dc_data);
    victim->dc_busy = 0;
    if (error != 0 || epoch != sc->ds_cache_epoch) {
        victim->dc_owner = 0;
        victim->dc_count = 0;
        if (error != 0)
            return error;
        return disk_backend_read_overlay(sc, absolute, requested, data);
    }
    victim->dc_start = region_start + window_relative;
    victim->dc_count = count;
    victim->dc_epoch = epoch;
    victim->dc_stamp = disk_cache_stamp();
    offset = (unsigned)(absolute - victim->dc_start);
    disk_copy(data, victim->dc_data + offset * DISK_SECTOR_SIZE,
        requested * DISK_SECTOR_SIZE);
    disk_write_cache_overlay(sc, absolute, requested, data);
    return 0;
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
    error = disk_write_cache_flush(sc);
    if (error != 0)
        return error;
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
        printf("%s%u: protective MBR, invalid GPT\n",
            disk_class_prefix(sc->ds_class), sc->ds_class_unit);
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
disk_find(unsigned disk_class, unsigned minor_number, unsigned *part_number)
{
    unsigned unit;
    unsigned i;

    unit = DISK_MINOR_UNIT(minor_number);
    if (unit >= DISK_MAX_UNITS)
        return 0;
    if (part_number != 0)
        *part_number = DISK_MINOR_PART(minor_number);
    for (i = 0; i < DISK_MAX_UNITS; ++i) {
        if (disk_softc[i].ds_used &&
            disk_softc[i].ds_class == disk_class &&
            disk_softc[i].ds_class_unit == unit)
            return &disk_softc[i];
    }
    return 0;
}

static struct disk_softc *
disk_lookup(unsigned disk_class, unsigned minor_number,
    unsigned *part_number)
{
    struct disk_softc *sc;

    sc = disk_find(disk_class, minor_number, part_number);
    if (sc == 0 || !disk_present(sc))
        return 0;
    return sc;
}

static int
disk_class_unit_alloc(unsigned disk_class, unsigned *unitp)
{
    unsigned i;
    unsigned unit;

    for (unit = 0; unit < DISK_MAX_UNITS; ++unit) {
        for (i = 0; i < DISK_MAX_UNITS; ++i) {
            if (disk_softc[i].ds_used &&
                disk_softc[i].ds_class == disk_class &&
                disk_softc[i].ds_class_unit == unit)
                break;
        }
        if (i == DISK_MAX_UNITS) {
            *unitp = unit;
            return 0;
        }
    }
    return ENOMEM;
}

int
disk_attach(const struct disk_attach_args *args, unsigned *handlep)
{
    struct disk_softc *sc;
    char sectors_text[24];
    char kbytes_text[24];
    char start_text[24];
    char count_text[24];
    const struct disk_partition *part;
    int error;
    unsigned i;
    unsigned class_unit;

    if (args == 0 || args->da_ops == 0 || args->da_ops->dbo_read == 0 ||
        args->da_sector_size != DISK_SECTOR_SIZE ||
        args->da_sector_count == 0 || args->da_class >= DISK_CLASS_COUNT)
        return EINVAL;
    for (i = 0; i < DISK_MAX_UNITS; ++i)
        if (!disk_softc[i].ds_used)
            break;
    if (i == DISK_MAX_UNITS)
        return ENOMEM;
    error = disk_class_unit_alloc(args->da_class, &class_unit);
    if (error != 0)
        return error;

    sc = &disk_softc[i];
    disk_zero(sc, sizeof(*sc));
    sc->ds_used = 1;
    sc->ds_attached = 1;
    sc->ds_unit = i;
    sc->ds_class = args->da_class;
    sc->ds_class_unit = class_unit;
    sc->ds_ops = args->da_ops;
    sc->ds_arg = args->da_arg;
    sc->ds_sector_count = args->da_sector_count;
    sc->ds_flags = args->da_flags;
    sc->ds_read_ahead_sectors = args->da_read_ahead_sectors;
    if (sc->ds_read_ahead_sectors > DISK_READ_AHEAD_MAX_SECTORS)
        sc->ds_read_ahead_sectors = DISK_READ_AHEAD_MAX_SECTORS;
    if (sc->ds_read_ahead_sectors != 0 &&
        (sc->ds_read_ahead_sectors &
        (sc->ds_read_ahead_sectors - 1u)) != 0)
        sc->ds_read_ahead_sectors = 0;
    sc->ds_write_back_sectors = args->da_write_back_sectors;
    if (sc->ds_write_back_sectors > DISK_WRITE_BACK_MAX_SECTORS)
        sc->ds_write_back_sectors = DISK_WRITE_BACK_MAX_SECTORS;
    if (sc->ds_write_back_sectors != 0 &&
        (sc->ds_write_back_sectors &
        (sc->ds_write_back_sectors - 1u)) != 0)
        sc->ds_write_back_sectors = 0;
    error = disk_revalidate(sc);
    if (error != 0) {
        disk_zero(sc, sizeof(*sc));
        return error;
    }

    printf("%s%u: %s 512-byte sectors (%s KB)%s%s\n",
        disk_class_prefix(sc->ds_class), sc->ds_class_unit,
        disk_lba_string(sectors_text, sc->ds_sector_count),
        disk_lba_string(kbytes_text, sc->ds_sector_count >> 1),
        (sc->ds_flags & DISK_FLAG_READ_ONLY) != 0 ? ", read-only" : "",
        (sc->ds_flags & DISK_FLAG_REMOVABLE) != 0 ? ", removable" : "");
    if (sc->ds_read_ahead_sectors != 0)
        printf("%s%u: read-ahead=%u sectors (%u KB)\n",
            disk_class_prefix(sc->ds_class), sc->ds_class_unit,
            sc->ds_read_ahead_sectors,
            sc->ds_read_ahead_sectors >> 1);
    if (sc->ds_table.dt_from_backup)
        printf("%s%u: using backup GPT; primary is invalid\n",
            disk_class_prefix(sc->ds_class), sc->ds_class_unit);
    for (i = 0; i < DISK_PARTITIONS; ++i) {
        part = &sc->ds_table.dt_partitions[i];
        if (part->dp_scheme == DISK_SCHEME_MBR)
            printf("%s%u%c: MBR type=%x start=%s sectors=%s\n",
                disk_class_prefix(sc->ds_class), sc->ds_class_unit,
                'a' + i, part->dp_type,
                disk_lba_string(start_text, part->dp_offset),
                disk_lba_string(count_text, part->dp_nsectors));
        else if (part->dp_scheme == DISK_SCHEME_GPT)
            printf("%s%u%c: GPT entry=%u start=%s sectors=%s\n",
                disk_class_prefix(sc->ds_class), sc->ds_class_unit,
                'a' + i, i + 1u,
                disk_lba_string(start_text, part->dp_offset),
                disk_lba_string(count_text, part->dp_nsectors));
    }
    if (handlep != 0)
        *handlep = sc->ds_unit;
    return 0;
}

void
disk_detach(unsigned handle, void *arg)
{
    struct disk_softc *sc;

    if (handle >= DISK_MAX_UNITS)
        return;
    sc = &disk_softc[handle];
    if (!sc->ds_used || sc->ds_arg != arg)
        return;
    disk_cache_invalidate(sc);
    disk_write_cache_discard(sc);
    sc->ds_attached = 0;
    printf("%s%u: detached\n", disk_class_prefix(sc->ds_class),
        sc->ds_class_unit);
    if (sc->ds_opens == 0)
        disk_zero(sc, sizeof(*sc));
}

void
diskattach(int unit)
{
    (void)unit;
    disk_zero(disk_softc, sizeof(disk_softc));
    disk_zero(disk_read_cache, sizeof(disk_read_cache));
    disk_zero(disk_write_cache, sizeof(disk_write_cache));
    disk_read_cache_clock = 0;
    printf("disk: block layer ready, MBR/GPT partitions, 64-bit LBA\n");
}

static int
disk_bdev_open_class(unsigned disk_class, dev_t dev, int flag, int mode)
{
    struct disk_softc *sc;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;

    (void)mode;
    sc = disk_lookup(disk_class, minor(dev), &part_number);
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

static int
disk_bdev_close_class(unsigned disk_class, dev_t dev, int flag, int mode)
{
    struct disk_softc *sc;
    int error;

    (void)flag;
    (void)mode;
    sc = disk_find(disk_class, minor(dev), 0);
    if (sc == 0 || !sc->ds_used || sc->ds_opens == 0)
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

static void
disk_bdev_strategy_class(unsigned disk_class, struct buf *bp)
{
    struct disk_softc *sc;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;
    disk_sector_t block;
    disk_sector_t relative;
    unsigned requested;
    int error;

    sc = disk_lookup(disk_class, minor(bp->b_dev), &part_number);
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
    /* Raw character I/O numbers B_PHYS requests in 512-byte sectors. */
    if ((bp->b_flags & B_PHYS) != 0)
        relative = block;
    else {
        if (block > (sectors >> 1)) {
            disk_bdev_done_error(bp, EINVAL);
            return;
        }
        relative = block << 1;
    }
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

    if ((bp->b_flags & B_READ) != 0) {
        if ((bp->b_flags & B_PHYS) != 0) {
            if (sc->ds_ops->dbo_read_phys != 0)
                error = sc->ds_ops->dbo_read_phys(sc->ds_arg,
                    start + relative, requested, bp->b_addr);
            else
                error = sc->ds_ops->dbo_read(sc->ds_arg,
                    start + relative, requested, bp->b_addr);
            if (error == 0)
                disk_write_cache_overlay(sc, start + relative, requested,
                    bp->b_addr);
        } else {
            error = disk_cached_read(sc, start, sectors, relative,
                requested, bp->b_addr);
        }
    } else {
        disk_cache_invalidate(sc);
        if ((bp->b_flags & B_PHYS) == 0 &&
            sc->ds_write_back_sectors != 0) {
            error = disk_write_cached(sc, start + relative, requested,
                bp->b_addr);
        } else {
            error = disk_write_cache_flush(sc);
            if (error == 0) {
                if ((bp->b_flags & B_PHYS) != 0 &&
                    sc->ds_ops->dbo_write_phys != 0)
                    error = sc->ds_ops->dbo_write_phys(sc->ds_arg,
                        start + relative, requested, bp->b_addr);
                else
                    error = sc->ds_ops->dbo_write(sc->ds_arg,
                        start + relative, requested, bp->b_addr);
            }
            if (error == 0)
                sc->ds_dirty = 1;
        }
    }
    if (error != 0) {
        disk_bdev_done_error(bp, error);
        return;
    }
    bp->b_resid = bp->b_bcount - requested * DISK_SECTOR_SIZE;
    biodone(bp);
}

static daddr_t
disk_bdev_size_class(unsigned disk_class, dev_t dev)
{
    struct disk_softc *sc;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;

    sc = disk_lookup(disk_class, minor(dev), &part_number);
    if (sc == 0 || disk_region(sc, part_number, &start, &sectors) != 0)
        return 0;
    if ((sectors >> 1) > 0x7fffffffu)
        return (daddr_t)0x7fffffff;
    return (daddr_t)(sectors >> 1);
}

static int
disk_bdev_ioctl_class(unsigned disk_class, dev_t dev, u_int cmd,
    caddr_t addr, int flag)
{
    struct disk_softc *sc;
    const struct disk_partition *part;
    struct diskpart64 *part64;
    unsigned part_number;
    disk_sector_t start;
    disk_sector_t sectors;
    int error;

    (void)flag;
    sc = disk_lookup(disk_class, minor(dev), &part_number);
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

int
disk_bdev_open(dev_t dev, int flag, int mode)
{
    return disk_bdev_open_class(DISK_CLASS_SD, dev, flag, mode);
}

int
disk_bdev_close(dev_t dev, int flag, int mode)
{
    return disk_bdev_close_class(DISK_CLASS_SD, dev, flag, mode);
}

void
disk_bdev_strategy(struct buf *bp)
{
    disk_bdev_strategy_class(DISK_CLASS_SD, bp);
}

daddr_t
disk_bdev_size(dev_t dev)
{
    return disk_bdev_size_class(DISK_CLASS_SD, dev);
}

int
disk_bdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return disk_bdev_ioctl_class(DISK_CLASS_SD, dev, cmd, addr, flag);
}

int
disk_wd_bdev_open(dev_t dev, int flag, int mode)
{
    return disk_bdev_open_class(DISK_CLASS_WD, dev, flag, mode);
}

int
disk_wd_bdev_close(dev_t dev, int flag, int mode)
{
    return disk_bdev_close_class(DISK_CLASS_WD, dev, flag, mode);
}

void
disk_wd_bdev_strategy(struct buf *bp)
{
    disk_bdev_strategy_class(DISK_CLASS_WD, bp);
}

daddr_t
disk_wd_bdev_size(dev_t dev)
{
    return disk_bdev_size_class(DISK_CLASS_WD, dev);
}

int
disk_wd_bdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return disk_bdev_ioctl_class(DISK_CLASS_WD, dev, cmd, addr, flag);
}

#ifndef DISK_HOST_TEST
static int
disk_cdev_rw(dev_t dev, struct uio *uio, int flag)
{
    if (uio->uio_offset < 0 ||
        ((disk_sector_t)uio->uio_offset & (DISK_SECTOR_SIZE - 1u)) != 0 ||
        (uio->uio_resid & (DISK_SECTOR_SIZE - 1u)) != 0)
        return EINVAL;
    return rawrw512(dev, uio, flag);
}

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
    return disk_cdev_rw(dev, uio, flag);
}

int
disk_cdev_write(dev_t dev, struct uio *uio, int flag)
{
    return disk_cdev_rw(dev, uio, flag);
}

int
disk_cdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return disk_bdev_ioctl(dev, cmd, addr, flag);
}

int
disk_wd_cdev_open(dev_t dev, int flag, int mode)
{
    return disk_wd_bdev_open(dev, flag, mode);
}

int
disk_wd_cdev_close(dev_t dev, int flag, int mode)
{
    return disk_wd_bdev_close(dev, flag, mode);
}

int
disk_wd_cdev_read(dev_t dev, struct uio *uio, int flag)
{
    return disk_cdev_rw(dev, uio, flag);
}

int
disk_wd_cdev_write(dev_t dev, struct uio *uio, int flag)
{
    return disk_cdev_rw(dev, uio, flag);
}

int
disk_wd_cdev_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    return disk_wd_bdev_ioctl(dev, cmd, addr, flag);
}
#endif
