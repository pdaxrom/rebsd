/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * Compact FAT16/FAT32 checker.
 *
 * The classic BSD fsck_msdos algorithm keeps a multi-word descriptor for
 * every cluster.  That is too large for small ReBSD targets, so this checker
 * reads FAT sectors on demand and keeps only a one-bit claimed-cluster map.
 */

#include <sys/types.h>
#ifdef REBSD_FSCK_DEVICE_IOCTL
#include <sys/disk.h>
#include <sys/ioctl.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/fat/fat.h>

#define FSCK_EXIT_OK               0
#define FSCK_EXIT_CORRECTED        1
#define FSCK_EXIT_UNCORRECTED      4
#define FSCK_EXIT_OPERATIONAL      8

#define FAT_PATH_SIZE              256u
#define FAT_LFN_SLOTS              20u
#define FAT_LFN_LAST               0x40u
#define FAT_LFN_ORDER_MASK         0x1fu

#define CHAIN_OK                   0
#define CHAIN_CROSS                1
#define CHAIN_INVALID              2

struct options {
    int always_no;
    int always_yes;
    int preen;
    int verbose;
};

struct fat_checker {
    const char *name;
    int fd;
    int read_only;
    int modified;
    int uncorrected;
    int fatal;
    int was_dirty;
    int had_io_error;
    struct options options;
    struct fat_volume declared_volume;
    struct fat_volume volume;
    unsigned media_sectors;
    unsigned char boot[FAT_SECTOR_SIZE];
    unsigned char *claimed;
    unsigned claimed_bytes;
    unsigned files;
    unsigned directories;
    unsigned free_clusters;
    unsigned bad_clusters;
    unsigned lost_clusters;
    unsigned cross_links;
    unsigned invalid_links;
    unsigned fat_cache_sector;
    int fat_cache_valid;
    unsigned char fat_cache[FAT_SECTOR_SIZE];
};

struct chain_result {
    unsigned clusters;
    unsigned status;
};

struct lfn_location {
    unsigned sector;
    unsigned slot;
};

struct lfn_state {
    int active;
    int valid;
    unsigned expected;
    unsigned checksum;
    unsigned count;
    struct lfn_location locations[FAT_LFN_SLOTS];
};

static unsigned
get_le16(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8);
}

static unsigned
get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void
put_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void
put_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static void
message(struct fat_checker *checker, const char *format, ...)
{
    va_list ap;

    if (checker != 0 && checker->name != 0)
        printf("%s: ", checker->name);
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

static void
operational_error(struct fat_checker *checker, const char *what)
{
    message(checker, "%s: %s\n", what, strerror(errno));
    checker->fatal = 1;
}

static int
get_media_sectors(int fd, unsigned *sectors)
{
    off_t current;
    off_t end;

    *sectors = 0;
#ifdef REBSD_FSCK_DEVICE_IOCTL
    {
        disk_sector_t sectors64;

        sectors64 = 0;
        if (ioctl(fd, DIOCGETSECTORS64, &sectors64) == 0 &&
            sectors64 != 0) {
            *sectors = sectors64 > (disk_sector_t)0xffffffffu ?
                0xffffffffu : (unsigned)sectors64;
            return 0;
        }
    }
#endif

    /* Regular images and host block devices commonly support SEEK_END. */
    current = lseek(fd, (off_t)0, SEEK_CUR);
    end = lseek(fd, (off_t)0, SEEK_END);
    if (current >= 0)
        (void)lseek(fd, current, SEEK_SET);
    if (end <= 0)
        return 0;
    if (end % FAT_SECTOR_SIZE != 0) {
        errno = EINVAL;
        return -1;
    }
    end /= FAT_SECTOR_SIZE;
    *sectors = end > (off_t)0xffffffffu ?
        0xffffffffu : (unsigned)end;
    return 0;
}

static int
want_fix(struct fat_checker *checker, int safe, const char *format, ...)
{
    char prompt[192];
    va_list ap;
    int ch;

    va_start(ap, format);
    (void)vsnprintf(prompt, sizeof(prompt), format, ap);
    va_end(ap);

    if (checker->read_only || checker->options.always_no) {
        printf("%s? no\n", prompt);
        checker->uncorrected = 1;
        return 0;
    }
    if (checker->options.always_yes) {
        printf("%s? yes\n", prompt);
        return 1;
    }
    if (checker->options.preen) {
        printf("%s? %s\n", prompt, safe ? "yes" : "no");
        if (!safe)
            checker->uncorrected = 1;
        return safe;
    }
    for (;;) {
        printf("%s? [yn] ", prompt);
        fflush(stdout);
        ch = getchar();
        if (ch == EOF) {
            checker->uncorrected = 1;
            return 0;
        }
        while (ch != '\n') {
            int next;

            next = getchar();
            if (next == EOF || next == '\n')
                break;
        }
        if (ch == 'y' || ch == 'Y')
            return 1;
        if (ch == 'n' || ch == 'N') {
            checker->uncorrected = 1;
            return 0;
        }
    }
}

static int
read_sector(struct fat_checker *checker, unsigned sector,
    unsigned char *data)
{
    off_t offset;
    ssize_t count;

    if (sector >= checker->volume.fv_total_sectors) {
        errno = EIO;
        operational_error(checker, "sector outside filesystem");
        return -1;
    }
    offset = (off_t)sector * FAT_SECTOR_SIZE;
    if (lseek(checker->fd, offset, SEEK_SET) != offset) {
        operational_error(checker, "seek");
        return -1;
    }
    count = read(checker->fd, data, FAT_SECTOR_SIZE);
    if (count != FAT_SECTOR_SIZE) {
        if (count >= 0)
            errno = EIO;
        operational_error(checker, "read sector");
        return -1;
    }
    return 0;
}

static int
write_sector(struct fat_checker *checker, unsigned sector,
    const unsigned char *data)
{
    off_t offset;
    ssize_t count;

    if (checker->read_only) {
        errno = EROFS;
        operational_error(checker, "write on read-only filesystem");
        return -1;
    }
    offset = (off_t)sector * FAT_SECTOR_SIZE;
    if (lseek(checker->fd, offset, SEEK_SET) != offset) {
        operational_error(checker, "seek for write");
        return -1;
    }
    count = write(checker->fd, data, FAT_SECTOR_SIZE);
    if (count != FAT_SECTOR_SIZE) {
        if (count >= 0)
            errno = EIO;
        operational_error(checker, "write sector");
        return -1;
    }
    checker->modified = 1;
    if (checker->fat_cache_valid && checker->fat_cache_sector == sector)
        checker->fat_cache_valid = 0;
    return 0;
}

static unsigned
fat_entry_size(const struct fat_checker *checker)
{
    return checker->volume.fv_type == FAT_TYPE_16 ? 2u : 4u;
}

static int
fat_position(struct fat_checker *checker, unsigned fat_index,
    unsigned cluster, unsigned *sector, unsigned *offset)
{
    unsigned byte_offset;
    unsigned size;

    size = fat_entry_size(checker);
    if (fat_index >= checker->volume.fv_fat_count ||
        cluster > checker->volume.fv_max_cluster)
        return -1;
    byte_offset = cluster * size;
    *sector = checker->volume.fv_reserved_sectors +
        fat_index * checker->volume.fv_fat_sectors +
        byte_offset / FAT_SECTOR_SIZE;
    *offset = byte_offset & (FAT_SECTOR_SIZE - 1u);
    if (*sector >= checker->volume.fv_reserved_sectors +
        (fat_index + 1u) * checker->volume.fv_fat_sectors ||
        *offset + size > FAT_SECTOR_SIZE)
        return -1;
    return 0;
}

static int
fat_read_from(struct fat_checker *checker, unsigned fat_index,
    unsigned cluster, unsigned *value)
{
    unsigned sector;
    unsigned offset;
    unsigned char data[FAT_SECTOR_SIZE];
    unsigned char *source;

    if (fat_position(checker, fat_index, cluster, &sector, &offset) < 0) {
        errno = EIO;
        operational_error(checker, "invalid FAT position");
        return -1;
    }
    if (fat_index == checker->volume.fv_active_fat) {
        if (!checker->fat_cache_valid ||
            checker->fat_cache_sector != sector) {
            if (read_sector(checker, sector, checker->fat_cache) < 0)
                return -1;
            checker->fat_cache_sector = sector;
            checker->fat_cache_valid = 1;
        }
        source = checker->fat_cache;
    } else {
        if (read_sector(checker, sector, data) < 0)
            return -1;
        source = data;
    }
    *value = fat_fat_decode(&checker->volume, source + offset);
    return 0;
}

static int
fat_read(struct fat_checker *checker, unsigned cluster, unsigned *value)
{
    return fat_read_from(checker, checker->volume.fv_active_fat,
        cluster, value);
}

static int
fat_read_geometry_from(struct fat_checker *checker,
    const struct fat_volume *volume, unsigned fat_index, unsigned cluster,
    unsigned *value)
{
    unsigned char data[FAT_SECTOR_SIZE];
    unsigned byte_offset;
    unsigned entry_size;
    unsigned sector;
    unsigned offset;

    entry_size = volume->fv_type == FAT_TYPE_16 ? 2u : 4u;
    if (fat_index >= volume->fv_fat_count ||
        cluster > volume->fv_max_cluster)
        return -1;
    byte_offset = cluster * entry_size;
    sector = volume->fv_reserved_sectors +
        fat_index * volume->fv_fat_sectors +
        byte_offset / FAT_SECTOR_SIZE;
    offset = byte_offset & (FAT_SECTOR_SIZE - 1u);
    if (sector >= volume->fv_reserved_sectors +
        (fat_index + 1u) * volume->fv_fat_sectors ||
        offset + entry_size > FAT_SECTOR_SIZE)
        return -1;
    if (read_sector(checker, sector, data) < 0)
        return -1;
    *value = fat_fat_decode(volume, data + offset);
    return 0;
}

static int
boot_geometry_matches(const unsigned char *primary,
    const unsigned char *backup)
{
    unsigned i;

    if (backup[510] != 0x55u || backup[511] != 0xaau)
        return 0;
    for (i = 11; i < 90u; ++i) {
        if (i >= 32u && i < 36u)
            continue;
        if (primary[i] != backup[i])
            return 0;
    }
    return 1;
}

static int
check_media_geometry(struct fat_checker *checker)
{
    unsigned char backup_data[FAT_SECTOR_SIZE];
    unsigned char primary_data[FAT_SECTOR_SIZE];
    unsigned cluster;
    unsigned fat;
    unsigned value;
    unsigned tail_allocated;
    unsigned boundary_links;
    unsigned backup;
    unsigned declared_clusters;
    unsigned declared_max_cluster;
    unsigned fat_entries;
    unsigned audited_max_cluster;
    unsigned audited_tail_clusters;
    unsigned unrepresented_tail_clusters;

    if (checker->volume.fv_declared_sectors <=
        checker->volume.fv_total_sectors)
        return 0;
    message(checker, "FAT%u boot sector declares %u sectors but "
        "device contains %u\n", checker->volume.fv_type,
        checker->volume.fv_declared_sectors,
        checker->volume.fv_total_sectors);
    if (checker->volume.fv_type != FAT_TYPE_32) {
        message(checker,
            "automatic geometry repair is supported only for FAT32\n");
        checker->uncorrected = 1;
        checker->read_only = 1;
        return 0;
    }

    declared_clusters = (checker->volume.fv_declared_sectors -
        checker->volume.fv_data_start) /
        checker->volume.fv_sectors_per_cluster;
    declared_max_cluster = declared_clusters + 1u;
    fat_entries = checker->volume.fv_fat_sectors *
        (FAT_SECTOR_SIZE / 4u);
    audited_max_cluster = declared_max_cluster;
    if (audited_max_cluster >= fat_entries)
        audited_max_cluster = fat_entries - 1u;
    if (audited_max_cluster < checker->volume.fv_max_cluster)
        audited_max_cluster = checker->volume.fv_max_cluster;
    audited_tail_clusters = audited_max_cluster -
        checker->volume.fv_max_cluster;
    unrepresented_tail_clusters = declared_max_cluster -
        audited_max_cluster;
    checker->declared_volume = checker->volume;
    checker->declared_volume.fv_total_sectors =
        checker->volume.fv_declared_sectors;
    checker->declared_volume.fv_cluster_count =
        audited_max_cluster - 1u;
    checker->declared_volume.fv_max_cluster = audited_max_cluster;

    tail_allocated = 0;
    for (cluster = checker->volume.fv_max_cluster + 1u;
        cluster <= audited_max_cluster; ++cluster) {
        for (fat = 0; fat < checker->declared_volume.fv_fat_count;
            ++fat) {
            if (fat_read_geometry_from(checker,
                &checker->declared_volume, fat, cluster, &value) < 0) {
                errno = EIO;
                operational_error(checker,
                    "read FAT entries beyond device geometry");
                return -1;
            }
            if (value != FAT_CLUSTER_FREE) {
                ++tail_allocated;
                break;
            }
        }
    }

    boundary_links = 0;
    for (cluster = 2; cluster <= checker->volume.fv_max_cluster;
        ++cluster) {
        if (fat_read_geometry_from(checker, &checker->declared_volume,
            checker->declared_volume.fv_active_fat, cluster, &value) < 0) {
            errno = EIO;
            operational_error(checker, "read FAT geometry links");
            return -1;
        }
        if (value > checker->volume.fv_max_cluster &&
            !fat_cluster_is_eoc(&checker->volume, value) &&
            !fat_cluster_is_bad(&checker->volume, value))
            ++boundary_links;
    }
    if (tail_allocated != 0 || boundary_links != 0) {
        message(checker,
            "cannot reduce FAT32 geometry: %u allocated tail cluster%s, "
            "%u link%s cross%s the device boundary\n",
            tail_allocated, tail_allocated == 1u ? "" : "s",
            boundary_links, boundary_links == 1u ? "" : "s",
            boundary_links == 1u ? "es" : "");
        message(checker,
            "no repairs will be written until those clusters are relocated\n");
        checker->uncorrected = 1;
        checker->read_only = 1;
        return 0;
    }

    backup = get_le16(checker->boot + 50);
    if (backup == 0 || backup == 0xffffu ||
        backup >= checker->volume.fv_reserved_sectors) {
        message(checker,
            "cannot safely repair geometry: no valid FAT32 backup "
            "boot sector is configured\n");
        checker->uncorrected = 1;
        checker->read_only = 1;
        return 0;
    }
    if (read_sector(checker, backup, backup_data) < 0)
        return -1;
    if (!boot_geometry_matches(checker->boot, backup_data)) {
        message(checker,
            "cannot safely repair geometry: FAT32 backup boot sector "
            "does not match primary\n");
        checker->uncorrected = 1;
        checker->read_only = 1;
        return 0;
    }

    if (unrepresented_tail_clusters != 0)
        message(checker, "%u declared tail cluster%s have no FAT "
            "entr%s and cannot be allocated\n",
            unrepresented_tail_clusters,
            unrepresented_tail_clusters == 1u ? "" : "s",
            unrepresented_tail_clusters == 1u ? "y" : "ies");
    message(checker, "%u %stail cluster%s are free; no FAT chain "
        "crosses the device boundary\n", audited_tail_clusters,
        unrepresented_tail_clusters == 0 ? "" : "representable ",
        audited_tail_clusters == 1u ? "" : "s");
    if (!want_fix(checker, 0, "Reduce FAT32 size from %u to %u sectors",
        checker->volume.fv_declared_sectors,
        checker->volume.fv_total_sectors)) {
        checker->read_only = 1;
        return 0;
    }

    memcpy(primary_data, checker->boot, sizeof(primary_data));
    put_le32(primary_data + 32, checker->volume.fv_total_sectors);
    put_le32(backup_data + 32, checker->volume.fv_total_sectors);
    if (write_sector(checker, backup, backup_data) < 0 ||
        write_sector(checker, 0, primary_data) < 0)
        return -1;
    memcpy(checker->boot, primary_data, sizeof(checker->boot));
    checker->volume.fv_declared_sectors =
        checker->volume.fv_total_sectors;
    checker->declared_volume = checker->volume;
    message(checker, "FAT32 primary and backup boot geometry updated\n");
    return 0;
}

static int
fat_write_to(struct fat_checker *checker, unsigned fat_index,
    unsigned cluster, unsigned value)
{
    unsigned sector;
    unsigned offset;
    unsigned char data[FAT_SECTOR_SIZE];

    if (fat_position(checker, fat_index, cluster, &sector, &offset) < 0) {
        errno = EIO;
        operational_error(checker, "invalid FAT write position");
        return -1;
    }
    if (read_sector(checker, sector, data) < 0)
        return -1;
    fat_fat_encode(&checker->volume, data + offset, value);
    return write_sector(checker, sector, data);
}

static int
fat_write(struct fat_checker *checker, unsigned cluster, unsigned value)
{
    unsigned first;
    unsigned count;
    unsigned i;

    if (checker->volume.fv_fat_mirrored) {
        first = 0;
        count = checker->volume.fv_fat_count;
    } else {
        first = checker->volume.fv_active_fat;
        count = 1;
    }
    for (i = 0; i < count; ++i)
        if (fat_write_to(checker, first + i, cluster, value) < 0)
            return -1;
    return 0;
}

static unsigned
fat_eoc_value(const struct fat_checker *checker)
{
    return checker->volume.fv_type == FAT_TYPE_16 ?
        0xffffu : 0x0fffffffu;
}

static int
fat_value_reserved(const struct fat_checker *checker, unsigned value)
{
    if (checker->volume.fv_type == FAT_TYPE_16)
        return value >= 0xfff0u && value <= 0xfff6u;
    return value >= 0x0ffffff0u && value <= 0x0ffffff6u;
}

static int
fat_value_allocated(const struct fat_checker *checker, unsigned value)
{
    (void)checker;
    return value != FAT_CLUSTER_FREE;
}

static int
claimed_test(const struct fat_checker *checker, unsigned cluster)
{
    return (checker->claimed[cluster >> 3] &
        (1u << (cluster & 7u))) != 0;
}

static void
claimed_set(struct fat_checker *checker, unsigned cluster)
{
    checker->claimed[cluster >> 3] |=
        (unsigned char)(1u << (cluster & 7u));
}

static void
claimed_clear(struct fat_checker *checker, unsigned cluster)
{
    checker->claimed[cluster >> 3] &=
        (unsigned char)~(1u << (cluster & 7u));
}

static int
compare_fats(struct fat_checker *checker)
{
    unsigned char source[FAT_SECTOR_SIZE];
    unsigned char other[FAT_SECTOR_SIZE];
    unsigned fat;
    unsigned sector;
    unsigned mismatches;
    unsigned source_sector;
    unsigned other_sector;

    if (checker->volume.fv_fat_count < 2u)
        return 0;
    if (!checker->volume.fv_fat_mirrored) {
        if (checker->options.verbose)
            message(checker, "FAT32 uses active FAT %u; mirroring disabled\n",
                checker->volume.fv_active_fat);
        return 0;
    }
    for (fat = 1; fat < checker->volume.fv_fat_count; ++fat) {
        mismatches = 0;
        for (sector = 0; sector < checker->volume.fv_fat_sectors;
            ++sector) {
            source_sector = checker->volume.fv_reserved_sectors + sector;
            other_sector = checker->volume.fv_reserved_sectors +
                fat * checker->volume.fv_fat_sectors + sector;
            if (read_sector(checker, source_sector, source) < 0 ||
                read_sector(checker, other_sector, other) < 0)
                return -1;
            if (memcmp(source, other, FAT_SECTOR_SIZE) != 0)
                ++mismatches;
        }
        if (mismatches == 0)
            continue;
        message(checker, "FAT %u differs from FAT 0 in %u sector%s\n",
            fat, mismatches, mismatches == 1u ? "" : "s");
        if (!want_fix(checker, 0, "Replace FAT %u from FAT 0", fat))
            continue;
        for (sector = 0; sector < checker->volume.fv_fat_sectors;
            ++sector) {
            source_sector = checker->volume.fv_reserved_sectors + sector;
            other_sector = checker->volume.fv_reserved_sectors +
                fat * checker->volume.fv_fat_sectors + sector;
            if (read_sector(checker, source_sector, source) < 0 ||
                write_sector(checker, other_sector, source) < 0)
                return -1;
        }
    }
    return 0;
}

static int
check_reserved_entries(struct fat_checker *checker)
{
    unsigned entry0;
    unsigned entry1;
    unsigned clean_mask;
    unsigned error_mask;

    if (fat_read(checker, 0, &entry0) < 0 ||
        fat_read(checker, 1, &entry1) < 0)
        return -1;
    if ((entry0 & 0xffu) != checker->boot[21] ||
        !fat_cluster_is_eoc(&checker->volume, entry0)) {
        message(checker, "invalid FAT media/reserved entry 0x%x\n",
            entry0);
        if (want_fix(checker, 1, "Repair FAT media entry") &&
            fat_write(checker, 0, fat_eoc_value(checker) -
            (0xffu - checker->boot[21])) < 0)
            return -1;
    }
    if (checker->volume.fv_type == FAT_TYPE_16) {
        clean_mask = 0x8000u;
        error_mask = 0x4000u;
        if ((entry1 & 0x3fffu) != 0x3fffu) {
            message(checker, "invalid FAT reserved entry 0x%x\n", entry1);
            if (want_fix(checker, 1, "Repair FAT reserved entry") &&
                fat_write(checker, 1, fat_eoc_value(checker)) < 0)
                return -1;
            entry1 = fat_eoc_value(checker);
        }
    } else {
        clean_mask = 0x08000000u;
        error_mask = 0x04000000u;
        if ((entry1 & 0x03ffffffu) != 0x03ffffffu) {
            message(checker, "invalid FAT reserved entry 0x%x\n", entry1);
            if (want_fix(checker, 1, "Repair FAT reserved entry") &&
                fat_write(checker, 1, fat_eoc_value(checker)) < 0)
                return -1;
            entry1 = fat_eoc_value(checker);
        }
    }
    checker->was_dirty = (entry1 & clean_mask) == 0;
    checker->had_io_error = (entry1 & error_mask) == 0;
    if (checker->was_dirty)
        message(checker, "filesystem dirty flag is set\n");
    if (checker->had_io_error)
        message(checker, "filesystem I/O-error flag is set\n");
    return 0;
}

static int
check_fat_entries(struct fat_checker *checker)
{
    unsigned cluster;
    unsigned value;
    unsigned invalid;

    checker->free_clusters = 0;
    checker->bad_clusters = 0;
    invalid = 0;
    for (cluster = 2; cluster <= checker->volume.fv_max_cluster;
        ++cluster) {
        if (fat_read(checker, cluster, &value) < 0)
            return -1;
        if (value == FAT_CLUSTER_FREE)
            ++checker->free_clusters;
        else if (fat_cluster_is_bad(&checker->volume, value))
            ++checker->bad_clusters;
        else if (!fat_cluster_is_eoc(&checker->volume, value) &&
            !fat_cluster_valid(&checker->volume, value))
            ++invalid;
    }
    checker->invalid_links = invalid;
    if (invalid != 0)
        message(checker, "%u FAT entr%s contain invalid next clusters\n",
            invalid, invalid == 1u ? "y" : "ies");
    return 0;
}

static int
walk_chain(struct fat_checker *checker, unsigned start, const char *path,
    struct chain_result *result)
{
    unsigned cluster;
    unsigned previous;
    unsigned next;
    unsigned steps;

    result->clusters = 0;
    result->status = CHAIN_OK;
    if (!fat_cluster_valid(&checker->volume, start)) {
        message(checker, "%s starts with invalid cluster %u\n", path, start);
        checker->uncorrected = 1;
        result->status = CHAIN_INVALID;
        return 0;
    }
    cluster = start;
    previous = 0;
    for (steps = 0; steps <= checker->volume.fv_cluster_count; ++steps) {
        if (claimed_test(checker, cluster)) {
            message(checker,
                "%s is cross-linked or loops at cluster %u\n",
                path, cluster);
            ++checker->cross_links;
            result->status = CHAIN_CROSS;
            if (previous != 0 &&
                want_fix(checker, 0,
                "Truncate %s before cluster %u", path, cluster)) {
                if (fat_write(checker, previous,
                    fat_eoc_value(checker)) < 0)
                    return -1;
            }
            return 0;
        }
        claimed_set(checker, cluster);
        ++result->clusters;
        if (fat_read(checker, cluster, &next) < 0)
            return -1;
        if (fat_cluster_is_eoc(&checker->volume, next))
            return 0;
        if (next == FAT_CLUSTER_FREE ||
            fat_cluster_is_bad(&checker->volume, next) ||
            fat_value_reserved(checker, next) ||
            !fat_cluster_valid(&checker->volume, next)) {
            message(checker, "%s chain ends at cluster %u with value 0x%x\n",
                path, cluster, next);
            result->status = CHAIN_INVALID;
            if (want_fix(checker, 1, "Terminate %s at cluster %u",
                path, cluster)) {
                if (fat_write(checker, cluster,
                    fat_eoc_value(checker)) < 0)
                    return -1;
            }
            return 0;
        }
        previous = cluster;
        cluster = next;
    }
    message(checker, "%s cluster chain exceeds filesystem size\n", path);
    checker->uncorrected = 1;
    result->status = CHAIN_INVALID;
    return 0;
}

static int
unclaim_chain(struct fat_checker *checker, unsigned start)
{
    unsigned cluster;
    unsigned next;
    unsigned steps;

    cluster = start;
    for (steps = 0; steps <= checker->volume.fv_cluster_count; ++steps) {
        if (!fat_cluster_valid(&checker->volume, cluster))
            return 0;
        claimed_clear(checker, cluster);
        if (fat_read(checker, cluster, &next) < 0)
            return -1;
        if (fat_cluster_is_eoc(&checker->volume, next) ||
            !fat_cluster_valid(&checker->volume, next))
            return 0;
        cluster = next;
    }
    return 0;
}

static int
truncate_chain(struct fat_checker *checker, unsigned start, unsigned keep)
{
    unsigned cluster;
    unsigned next;
    unsigned index;

    if (keep == 0)
        return unclaim_chain(checker, start);
    cluster = start;
    for (index = 1; index < keep; ++index) {
        if (fat_read(checker, cluster, &next) < 0)
            return -1;
        if (!fat_cluster_valid(&checker->volume, next))
            return 0;
        cluster = next;
    }
    if (fat_read(checker, cluster, &next) < 0)
        return -1;
    if (fat_cluster_is_eoc(&checker->volume, next))
        return 0;
    if (fat_write(checker, cluster, fat_eoc_value(checker)) < 0)
        return -1;
    return unclaim_chain(checker, next);
}

static void
lfn_reset(struct lfn_state *state)
{
    memset(state, 0, sizeof(*state));
}

static void
lfn_add(struct lfn_state *state, const unsigned char *entry,
    unsigned sector, unsigned slot)
{
    unsigned order;

    order = entry[0] & FAT_LFN_ORDER_MASK;
    if ((entry[0] & FAT_LFN_LAST) != 0) {
        lfn_reset(state);
        state->active = 1;
        state->valid = order != 0 && order <= FAT_LFN_SLOTS;
        state->expected = order;
        state->checksum = entry[13];
    } else if (!state->active) {
        state->active = 1;
        state->valid = 0;
    }
    if (state->count < FAT_LFN_SLOTS) {
        state->locations[state->count].sector = sector;
        state->locations[state->count].slot = slot;
        ++state->count;
    } else {
        state->valid = 0;
    }
    if (order == 0 || order != state->expected ||
        (entry[0] & ~(FAT_LFN_LAST | FAT_LFN_ORDER_MASK)) != 0 ||
        entry[12] != 0 || get_le16(entry + 26) != 0 ||
        entry[13] != state->checksum)
        state->valid = 0;
    if (state->expected != 0)
        --state->expected;
}

static int
delete_lfn(struct fat_checker *checker, struct lfn_state *state,
    unsigned current_sector, unsigned char *current_data, const char *path)
{
    unsigned char data[FAT_SECTOR_SIZE];
    unsigned sector;
    unsigned slot;
    unsigned i;
    int changed;

    if (!state->active)
        return 0;
    message(checker, "invalid long-name records in %s\n", path);
    if (!want_fix(checker, 1, "Delete invalid long-name records"))
        return 0;
    for (i = 0; i < state->count; ++i) {
        sector = state->locations[i].sector;
        slot = state->locations[i].slot;
        if (current_data != 0 && sector == current_sector) {
            current_data[slot * FAT_DIRENT_SIZE] = FAT_DIRENT_DELETED;
            continue;
        }
        if (read_sector(checker, sector, data) < 0)
            return -1;
        changed = data[slot * FAT_DIRENT_SIZE] != FAT_DIRENT_DELETED;
        data[slot * FAT_DIRENT_SIZE] = FAT_DIRENT_DELETED;
        if (changed && write_sector(checker, sector, data) < 0)
            return -1;
    }
    return 1;
}

static int
dirent_set_cluster_size(unsigned char *entry, unsigned cluster,
    unsigned size)
{
    unsigned old_cluster;
    unsigned old_size;

    old_cluster = (get_le16(entry + 20) << 16) | get_le16(entry + 26);
    old_size = get_le32(entry + 28);
    if (old_cluster == cluster && old_size == size)
        return 0;
    put_le16(entry + 20, cluster >> 16);
    put_le16(entry + 26, cluster);
    put_le32(entry + 28, size);
    return 1;
}

static int scan_directory(struct fat_checker *, unsigned, unsigned,
    const char *, unsigned, int);

static int
process_entry(struct fat_checker *checker, unsigned char *entry,
    unsigned current_cluster, unsigned parent_cluster, const char *dirpath,
    unsigned depth, int is_root)
{
    struct fat_dirent dirent;
    struct chain_result chain;
    char name[16];
    char path[FAT_PATH_SIZE];
    unsigned cluster_size;
    unsigned expected;
    unsigned maximum_size;
    unsigned expected_dot;
    int changed;
    int is_dot;
    int is_dotdot;

    changed = 0;
    is_dot = memcmp(entry, ".          ", 11) == 0;
    is_dotdot = memcmp(entry, "..         ", 11) == 0;
    fat_dirent_parse(&dirent, entry);
    if (fat_short_name(entry, name, sizeof(name)) < 0)
        strcpy(name, "?");
    if (snprintf(path, sizeof(path), "%s%s%s", dirpath,
        strcmp(dirpath, "/") == 0 ? "" : "/", name) >=
        (int)sizeof(path))
        strcpy(path, "<path-too-long>");

    if (checker->volume.fv_type == FAT_TYPE_16 &&
        get_le16(entry + 20) != 0) {
        message(checker, "%s has non-zero FAT16 high cluster word\n", path);
        if (want_fix(checker, 1, "Clear high cluster word for %s", path)) {
            put_le16(entry + 20, 0);
            dirent.fd_cluster &= 0xffffu;
            changed = 1;
        }
    }

    if (is_dot || is_dotdot) {
        if (is_root) {
            message(checker, "root directory contains %s entry\n", name);
            checker->uncorrected = 1;
            return changed;
        }
        expected_dot = is_dot ? current_cluster : parent_cluster;
        if (is_dotdot && parent_cluster == checker->volume.fv_root_cluster)
            expected_dot = 0;
        if ((dirent.fd_attr & FAT_ATTR_DIRECTORY) == 0 ||
            dirent.fd_cluster != expected_dot || dirent.fd_size != 0) {
            message(checker, "%s entry is inconsistent\n", path);
            if (want_fix(checker, 1, "Repair %s entry", path)) {
                entry[11] |= FAT_ATTR_DIRECTORY;
                changed |= dirent_set_cluster_size(entry, expected_dot, 0);
            }
        }
        return changed;
    }

    if ((dirent.fd_attr & FAT_ATTR_DIRECTORY) != 0) {
        ++checker->directories;
        if (dirent.fd_size != 0) {
            message(checker, "%s directory size is %u, not zero\n",
                path, dirent.fd_size);
            if (want_fix(checker, 1, "Clear directory size for %s", path)) {
                put_le32(entry + 28, 0);
                changed = 1;
            }
        }
        if (!fat_cluster_valid(&checker->volume, dirent.fd_cluster)) {
            message(checker, "%s directory has invalid start cluster %u\n",
                path, dirent.fd_cluster);
            checker->uncorrected = 1;
            return changed;
        }
        if (depth >= 64u) {
            message(checker, "%s exceeds maximum directory depth\n", path);
            checker->uncorrected = 1;
            return changed;
        }
        if (scan_directory(checker, dirent.fd_cluster, current_cluster,
            path, depth + 1u, 0) < 0)
            return -1;
        return changed;
    }

    ++checker->files;
    if (dirent.fd_size == 0) {
        if (dirent.fd_cluster != 0) {
            message(checker, "%s has size zero but starts at cluster %u\n",
                path, dirent.fd_cluster);
            if (want_fix(checker, 1, "Detach zero-length chain from %s",
                path)) {
                changed |= dirent_set_cluster_size(entry, 0, 0);
            } else if (walk_chain(checker, dirent.fd_cluster, path,
                &chain) < 0) {
                return -1;
            }
        }
        return changed;
    }
    if (!fat_cluster_valid(&checker->volume, dirent.fd_cluster)) {
        message(checker, "%s has size %u but invalid start cluster %u\n",
            path, dirent.fd_size, dirent.fd_cluster);
        if (want_fix(checker, 0, "Truncate %s to zero bytes", path))
            changed |= dirent_set_cluster_size(entry, 0, 0);
        return changed;
    }
    if (walk_chain(checker, dirent.fd_cluster, path, &chain) < 0)
        return -1;
    if (chain.status == CHAIN_CROSS && chain.clusters == 0) {
        if (want_fix(checker, 0, "Detach cross-linked file %s", path))
            changed |= dirent_set_cluster_size(entry, 0, 0);
        return changed;
    }
    cluster_size = checker->volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    expected = dirent.fd_size / cluster_size;
    if (dirent.fd_size % cluster_size != 0)
        ++expected;
    if (chain.clusters < expected) {
        maximum_size = chain.clusters * cluster_size;
        message(checker, "%s size %u exceeds its %u-cluster chain\n",
            path, dirent.fd_size, chain.clusters);
        if (want_fix(checker, 1, "Reduce %s size to %u", path,
            maximum_size)) {
            put_le32(entry + 28, maximum_size);
            changed = 1;
        }
    } else if (chain.clusters > expected && chain.status == CHAIN_OK) {
        message(checker, "%s uses %u clusters but needs %u\n",
            path, chain.clusters, expected);
        if (want_fix(checker, 1, "Truncate excess chain for %s", path)) {
            if (truncate_chain(checker, dirent.fd_cluster, expected) < 0)
                return -1;
        }
    }
    return changed;
}

static int
scan_directory_sector(struct fat_checker *checker, unsigned sector,
    unsigned current_cluster, unsigned parent_cluster, const char *path,
    unsigned depth, int is_root, struct lfn_state *lfn, int *stop)
{
    unsigned char data[FAT_SECTOR_SIZE];
    unsigned char *entry;
    unsigned slot;
    unsigned attr;
    int changed;
    int result;

    if (read_sector(checker, sector, data) < 0)
        return -1;
    changed = 0;
    for (slot = 0; slot < FAT_DIRENTS_PER_SECTOR; ++slot) {
        entry = data + slot * FAT_DIRENT_SIZE;
        if (entry[0] == FAT_DIRENT_END) {
            if (lfn->active) {
                result = delete_lfn(checker, lfn, sector, data, path);
                if (result < 0)
                    return -1;
                changed |= result;
                lfn_reset(lfn);
            }
            *stop = 1;
            break;
        }
        if (entry[0] == FAT_DIRENT_DELETED) {
            if (lfn->active) {
                result = delete_lfn(checker, lfn, sector, data, path);
                if (result < 0)
                    return -1;
                changed |= result;
                lfn_reset(lfn);
            }
            continue;
        }
        attr = entry[11];
        if (attr == FAT_ATTR_LONG_NAME) {
            if (lfn->active && (entry[0] & FAT_LFN_LAST) != 0) {
                result = delete_lfn(checker, lfn, sector, data, path);
                if (result < 0)
                    return -1;
                changed |= result;
                lfn_reset(lfn);
            }
            lfn_add(lfn, entry, sector, slot);
            continue;
        }
        if (lfn->active) {
            if (!lfn->valid || lfn->expected != 0 ||
                lfn->checksum != fat_lfn_checksum(entry)) {
                result = delete_lfn(checker, lfn, sector, data, path);
                if (result < 0)
                    return -1;
                changed |= result;
            }
            lfn_reset(lfn);
        }
        if ((attr & FAT_ATTR_VOLUME_ID) != 0)
            continue;
        result = process_entry(checker, entry, current_cluster,
            parent_cluster, path, depth, is_root);
        if (result < 0)
            return -1;
        changed |= result;
    }
    if (changed && write_sector(checker, sector, data) < 0)
        return -1;
    return 0;
}

static int
scan_directory(struct fat_checker *checker, unsigned start,
    unsigned parent_cluster, const char *path, unsigned depth, int is_root)
{
    struct chain_result chain;
    struct lfn_state lfn;
    unsigned cluster;
    unsigned next;
    unsigned sector;
    unsigned i;
    unsigned steps;
    int stop;

    lfn_reset(&lfn);
    stop = 0;
    if (is_root && checker->volume.fv_type == FAT_TYPE_16) {
        for (i = 0; i < checker->volume.fv_root_dir_sectors && !stop; ++i)
            if (scan_directory_sector(checker,
                checker->volume.fv_root_dir_start + i, 0, 0, path,
                depth, 1, &lfn, &stop) < 0)
                return -1;
        if (lfn.active) {
            int result;

            result = delete_lfn(checker, &lfn, ~0u, 0, path);
            if (result < 0)
                return -1;
            lfn_reset(&lfn);
        }
        return 0;
    }
    if (walk_chain(checker, start, path, &chain) < 0)
        return -1;
    if (chain.clusters == 0) {
        checker->uncorrected = 1;
        return 0;
    }
    cluster = start;
    for (steps = 0; steps < chain.clusters && !stop; ++steps) {
        sector = fat_cluster_first_sector(&checker->volume, cluster);
        for (i = 0; i < checker->volume.fv_sectors_per_cluster && !stop;
            ++i)
            if (scan_directory_sector(checker, sector + i, start,
                parent_cluster, path, depth, is_root, &lfn, &stop) < 0)
                return -1;
        if (fat_read(checker, cluster, &next) < 0)
            return -1;
        if (fat_cluster_is_eoc(&checker->volume, next) ||
            !fat_cluster_valid(&checker->volume, next))
            break;
        cluster = next;
    }
    if (lfn.active) {
        int result;

        result = delete_lfn(checker, &lfn, ~0u, 0, path);
        if (result < 0)
            return -1;
        lfn_reset(&lfn);
    }
    return 0;
}

static int
check_lost_clusters(struct fat_checker *checker)
{
    unsigned cluster;
    unsigned value;
    unsigned lost;

    lost = 0;
    for (cluster = 2; cluster <= checker->volume.fv_max_cluster;
        ++cluster) {
        if (fat_read(checker, cluster, &value) < 0)
            return -1;
        if (fat_value_allocated(checker, value) &&
            !fat_cluster_is_bad(&checker->volume, value) &&
            !claimed_test(checker, cluster))
            ++lost;
    }
    checker->lost_clusters = lost;
    if (lost == 0)
        return 0;
    message(checker, "%u lost cluster%s\n", lost, lost == 1u ? "" : "s");
    if (!want_fix(checker, 0, "Clear all lost clusters"))
        return 0;
    for (cluster = 2; cluster <= checker->volume.fv_max_cluster;
        ++cluster) {
        if (fat_read(checker, cluster, &value) < 0)
            return -1;
        if (fat_value_allocated(checker, value) &&
            !fat_cluster_is_bad(&checker->volume, value) &&
            !claimed_test(checker, cluster))
            if (fat_write(checker, cluster, FAT_CLUSTER_FREE) < 0)
                return -1;
    }
    checker->lost_clusters = 0;
    return 0;
}

static int
recount_free(struct fat_checker *checker, unsigned *free_count,
    unsigned *next_free)
{
    unsigned cluster;
    unsigned value;

    *free_count = 0;
    *next_free = 0xffffffffu;
    for (cluster = 2; cluster <= checker->volume.fv_max_cluster;
        ++cluster) {
        if (fat_read(checker, cluster, &value) < 0)
            return -1;
        if (value == FAT_CLUSTER_FREE) {
            ++*free_count;
            if (*next_free == 0xffffffffu)
                *next_free = cluster;
        }
    }
    return 0;
}

static int
fsinfo_valid(const unsigned char *data)
{
    return get_le32(data) == 0x41615252u &&
        get_le32(data + 484) == 0x61417272u &&
        get_le32(data + 508) == 0xaa550000u;
}

static int
check_fsinfo(struct fat_checker *checker)
{
    unsigned char data[FAT_SECTOR_SIZE];
    unsigned fsinfo;
    unsigned backup;
    unsigned backup_fsinfo;
    unsigned stored_free;
    unsigned stored_next;
    unsigned actual_free;
    unsigned actual_next;
    unsigned stored_next_value;
    int valid;
    int next_invalid;
    int needs_update;

    if (checker->volume.fv_type != FAT_TYPE_32)
        return 0;
    fsinfo = get_le16(checker->boot + 48);
    if (fsinfo == 0 || fsinfo == 0xffffu ||
        fsinfo >= checker->volume.fv_reserved_sectors)
        return 0;
    if (read_sector(checker, fsinfo, data) < 0)
        return -1;
    valid = fsinfo_valid(data);
    stored_free = valid ? get_le32(data + 488) : 0xffffffffu;
    stored_next = valid ? get_le32(data + 492) : 0xffffffffu;
    if (recount_free(checker, &actual_free, &actual_next) < 0)
        return -1;
    checker->free_clusters = actual_free;
    next_invalid = 0;
    if (stored_next != 0xffffffffu) {
        if (actual_free == 0 || stored_next < 2u ||
            stored_next > checker->volume.fv_max_cluster) {
            next_invalid = 1;
        } else {
            if (fat_read(checker, stored_next, &stored_next_value) < 0)
                return -1;
            next_invalid = stored_next_value != FAT_CLUSTER_FREE;
        }
    }
    needs_update = !valid ||
        (stored_free != 0xffffffffu && stored_free != actual_free) ||
        next_invalid;
    if (!valid)
        message(checker, "invalid FAT32 FSInfo signatures\n");
    else if (stored_free != 0xffffffffu && stored_free != actual_free)
        message(checker, "FSInfo free count %u should be %u\n",
            stored_free, actual_free);
    else if (next_invalid)
        message(checker, "FSInfo next-free cluster %u is not free\n",
            stored_next);
    backup = get_le16(checker->boot + 50);
    backup_fsinfo = backup + fsinfo;
    if (needs_update) {
        if (!want_fix(checker, 1, "Update FAT32 FSInfo"))
            return 0;
        memset(data, 0, sizeof(data));
        put_le32(data, 0x41615252u);
        put_le32(data + 484, 0x61417272u);
        put_le32(data + 488, actual_free);
        put_le32(data + 492, actual_next);
        put_le32(data + 508, 0xaa550000u);
        if (write_sector(checker, fsinfo, data) < 0)
            return -1;
        if (backup != 0 && backup != 0xffffu &&
            backup_fsinfo < checker->volume.fv_reserved_sectors)
            if (write_sector(checker, backup_fsinfo, data) < 0)
                return -1;
    }
    return 0;
}

static int
check_backup_boot(struct fat_checker *checker)
{
    unsigned char backup_data[FAT_SECTOR_SIZE];
    unsigned backup;

    if (checker->volume.fv_type != FAT_TYPE_32)
        return 0;
    backup = get_le16(checker->boot + 50);
    if (backup == 0 || backup == 0xffffu ||
        backup >= checker->volume.fv_reserved_sectors)
        return 0;
    if (read_sector(checker, backup, backup_data) < 0)
        return -1;
    if (memcmp(checker->boot + 11, backup_data + 11, 79) == 0 &&
        backup_data[510] == 0x55u && backup_data[511] == 0xaau)
        return 0;
    message(checker, "FAT32 backup boot sector differs from primary\n");
    if (want_fix(checker, 1, "Replace backup boot sector") &&
        write_sector(checker, backup, checker->boot) < 0)
        return -1;
    return 0;
}

static int
mark_clean(struct fat_checker *checker)
{
    if (!checker->was_dirty && !checker->had_io_error)
        return 0;
    if (checker->uncorrected) {
        message(checker, "filesystem remains marked dirty\n");
        return 0;
    }
    if (!want_fix(checker, 1, "Mark filesystem clean"))
        return 0;
    return fat_write(checker, 1, fat_eoc_value(checker));
}

static int
check_one(const char *name, const struct options *options)
{
    struct fat_checker checker;
    unsigned next_free;
    int parse_result;
    int flags;
    int result;

    memset(&checker, 0, sizeof(checker));
    checker.name = name;
    checker.fd = -1;
    checker.options = *options;
    checker.read_only = options->always_no;
    flags = checker.read_only ? O_RDONLY : O_RDWR;
    checker.fd = open(name, flags);
    if (checker.fd < 0 && !checker.read_only) {
        checker.fd = open(name, O_RDONLY);
        if (checker.fd >= 0) {
            checker.read_only = 1;
            message(&checker, "opened read-only\n");
        }
    }
    if (checker.fd < 0) {
        operational_error(&checker, "open");
        return FSCK_EXIT_OPERATIONAL;
    }
    if (get_media_sectors(checker.fd, &checker.media_sectors) < 0) {
        operational_error(&checker, "determine media size");
        close(checker.fd);
        return FSCK_EXIT_OPERATIONAL;
    }
    memset(&checker.volume, 0, sizeof(checker.volume));
    if (lseek(checker.fd, (off_t)0, SEEK_SET) != 0 ||
        read(checker.fd, checker.boot, FAT_SECTOR_SIZE) != FAT_SECTOR_SIZE) {
        operational_error(&checker, "read boot sector");
        close(checker.fd);
        return FSCK_EXIT_OPERATIONAL;
    }
    parse_result = fat_volume_parse(&checker.volume, checker.boot,
        checker.media_sectors);
    if (parse_result != FAT_PARSE_OK) {
        message(&checker, "%s FAT geometry for media boundary\n",
            parse_result == FAT_PARSE_UNSUPPORTED ?
            "unsupported" : "invalid");
        close(checker.fd);
        return FSCK_EXIT_OPERATIONAL;
    }

    message(&checker, "FAT%u, %u sectors, %u sectors/cluster, %u FAT%s\n",
        checker.volume.fv_type, checker.volume.fv_total_sectors,
        checker.volume.fv_sectors_per_cluster,
        checker.volume.fv_fat_count,
        checker.volume.fv_fat_count == 1u ? "" : "s");
    if (check_media_geometry(&checker) < 0)
        goto done;
    checker.claimed_bytes =
        (checker.volume.fv_max_cluster + 8u) >> 3;
    checker.claimed = calloc(checker.claimed_bytes, 1);
    if (checker.claimed == 0) {
        message(&checker, "cannot allocate %u-byte cluster bitmap\n",
            checker.claimed_bytes);
        close(checker.fd);
        return FSCK_EXIT_OPERATIONAL;
    }
    printf("** Phase 1 - Read and compare FATs\n");
    if (compare_fats(&checker) < 0 ||
        check_reserved_entries(&checker) < 0 ||
        check_fat_entries(&checker) < 0)
        goto done;
    printf("** Phase 2 - Check directories and cluster chains\n");
    if (scan_directory(&checker, checker.volume.fv_root_cluster,
        checker.volume.fv_root_cluster, "/", 0, 1) < 0)
        goto done;
    printf("** Phase 3 - Check for lost clusters\n");
    if (check_lost_clusters(&checker) < 0)
        goto done;
    printf("** Phase 4 - Check FAT32 metadata\n");
    if (check_fsinfo(&checker) < 0 ||
        check_backup_boot(&checker) < 0 ||
        mark_clean(&checker) < 0)
        goto done;
    if (recount_free(&checker, &checker.free_clusters, &next_free) < 0)
        goto done;
    message(&checker,
        "%u file%s, %u director%s, %u free cluster%s, %u bad cluster%s\n",
        checker.files, checker.files == 1u ? "" : "s",
        checker.directories,
        checker.directories == 1u ? "y" : "ies",
        checker.free_clusters, checker.free_clusters == 1u ? "" : "s",
        checker.bad_clusters, checker.bad_clusters == 1u ? "" : "s");

done:
    if (checker.modified && !checker.read_only)
        (void)fsync(checker.fd);
    if (checker.fd >= 0)
        close(checker.fd);
    free(checker.claimed);
    if (checker.fatal)
        result = FSCK_EXIT_OPERATIONAL;
    else if (checker.uncorrected)
        result = FSCK_EXIT_UNCORRECTED;
    else if (checker.modified)
        result = FSCK_EXIT_CORRECTED;
    else
        result = FSCK_EXIT_OK;
    if (checker.modified)
        message(&checker, "FILESYSTEM WAS MODIFIED\n");
    if (checker.uncorrected)
        message(&checker, "FILESYSTEM HAS UNCORRECTED ERRORS\n");
    return result;
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: fsck.fat [-fnpvy] filesystem [filesystem ...]\n");
    exit(FSCK_EXIT_OPERATIONAL);
}

int
main(int argc, char **argv)
{
    struct options options;
    int ch;
    int status;
    int current;

    memset(&options, 0, sizeof(options));
    while ((ch = getopt(argc, argv, "fnpvy")) != -1) {
        switch (ch) {
        case 'f':
            break;
        case 'n':
            options.always_no = 1;
            options.always_yes = 0;
            options.preen = 0;
            break;
        case 'p':
            options.preen = 1;
            options.always_no = 0;
            options.always_yes = 0;
            break;
        case 'v':
            options.verbose = 1;
            break;
        case 'y':
            options.always_yes = 1;
            options.always_no = 0;
            options.preen = 0;
            break;
        default:
            usage();
        }
    }
    if (optind >= argc)
        usage();
    status = FSCK_EXIT_OK;
    while (optind < argc) {
        current = check_one(argv[optind++], &options);
        if (current == FSCK_EXIT_OPERATIONAL)
            status = FSCK_EXIT_OPERATIONAL;
        else if (status != FSCK_EXIT_OPERATIONAL && current > status)
            status = current;
    }
    return status;
}
