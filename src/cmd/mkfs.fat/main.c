/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * Compact FAT16/FAT32 formatter.  Metadata is emitted one sector at a time,
 * so formatting a large device does not require keeping its FAT in memory.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef MKFS_FAT_HOST
#include <sys/disk.h>
#include <sys/ioctl.h>
#endif

#define SECTOR_SIZE             512u
#define FAT_COUNT               2u
#define FAT16_ROOT_ENTRIES      512u
#define FAT16_ROOT_SECTORS      32u
#define FAT16_MIN_CLUSTERS      4085u
#define FAT16_MAX_CLUSTERS      65524u
#define FAT32_MIN_CLUSTERS      65525u
#define FAT32_MAX_CLUSTERS      0x0ffffff5u
#define FAT16_RESERVED          1u
#define FAT32_RESERVED          32u
#define FAT32_ROOT_CLUSTER      2u
#define FAT32_FSINFO_SECTOR     1u
#define FAT32_BACKUP_SECTOR     6u
#define MEDIA_FIXED             0xf8u

struct geometry {
    unsigned type;
    unsigned total_sectors;
    unsigned sectors_per_cluster;
    unsigned reserved_sectors;
    unsigned root_sectors;
    unsigned fat_sectors;
    unsigned data_start;
    unsigned clusters;
};

struct options {
    unsigned type;
    unsigned sectors_per_cluster;
    unsigned volume_id;
    int volume_id_set;
    int label_set;
    int no_write;
    int verbose;
    unsigned char label[11];
};

static const char *program_name = "mkfs.fat";

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
usage(void)
{
    fprintf(stderr,
        "usage: %s [-Nv] [-F 16|32] [-i volume-id] [-n label] "
        "[-s sectors-per-cluster] special\n", program_name);
    exit(1);
}

static int
parse_unsigned(const char *text, unsigned limit, unsigned *value)
{
    char *end;
    unsigned long parsed;

    if (text == 0 || text[0] == '\0' || text[0] == '-')
        return -1;
    end = 0;
    parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed > limit)
        return -1;
    *value = (unsigned)parsed;
    return 0;
}

static int
power_of_two(unsigned value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static int
label_character(unsigned char ch)
{
    static const char invalid[] = "\"*+,./:;<=>?[\\]|";

    return ch >= 0x20u && ch != 0x7fu && strchr(invalid, ch) == 0;
}

static int
make_label(const char *text, unsigned char label[11])
{
    unsigned char ch;
    unsigned length;
    unsigned i;

    length = (unsigned)strlen(text);
    if (length == 0 || length > 11u)
        return -1;
    memset(label, ' ', 11);
    for (i = 0; i < length; ++i) {
        ch = (unsigned char)text[i];
        if (!label_character(ch))
            return -1;
        if (ch >= 'a' && ch <= 'z')
            ch = (unsigned char)(ch - 'a' + 'A');
        label[i] = ch;
    }
    return 0;
}

static unsigned
ceil_div(unsigned numerator, unsigned denominator)
{
    return numerator / denominator +
        (numerator % denominator != 0 ? 1u : 0u);
}

/*
 * Choose the smallest FAT that can describe the resulting data area.  The
 * predicate is monotonic, so a binary search avoids slow convergence on very
 * large FAT32 media.
 */
static int
compute_geometry(unsigned total, unsigned type, unsigned spc,
    struct geometry *geometry)
{
    unsigned reserved;
    unsigned root;
    unsigned entry_size;
    unsigned fixed;
    unsigned low;
    unsigned high;
    unsigned middle;
    unsigned data;
    unsigned clusters;
    unsigned needed;

    reserved = type == 16u ? FAT16_RESERVED : FAT32_RESERVED;
    root = type == 16u ? FAT16_ROOT_SECTORS : 0u;
    entry_size = type == 16u ? 2u : 4u;
    if (total <= reserved + root + FAT_COUNT)
        return -1;
    fixed = reserved + root;
    data = total - fixed - FAT_COUNT;
    clusters = data / spc;
    if (clusters > (0xffffffffu / entry_size) - 2u)
        return -1;
    high = ceil_div((clusters + 2u) * entry_size, SECTOR_SIZE);
    if (high == 0)
        high = 1;
    low = 1;
    while (low < high) {
        middle = low + (high - low) / 2u;
        if (middle > (total - fixed) / FAT_COUNT) {
            low = middle + 1u;
            continue;
        }
        data = total - fixed - FAT_COUNT * middle;
        clusters = data / spc;
        needed = ceil_div((clusters + 2u) * entry_size, SECTOR_SIZE);
        if (needed <= middle)
            high = middle;
        else
            low = middle + 1u;
    }
    if (low > (total - fixed) / FAT_COUNT)
        return -1;
    data = total - fixed - FAT_COUNT * low;
    clusters = data / spc;
    needed = ceil_div((clusters + 2u) * entry_size, SECTOR_SIZE);
    if (needed > low || clusters == 0)
        return -1;
    if (type == 16u) {
        if (clusters < FAT16_MIN_CLUSTERS)
            return 1;
        if (clusters > FAT16_MAX_CLUSTERS || low > 0xffffu)
            return 2;
    } else {
        if (clusters < FAT32_MIN_CLUSTERS)
            return 1;
        if (clusters > FAT32_MAX_CLUSTERS)
            return 2;
    }
    memset(geometry, 0, sizeof(*geometry));
    geometry->type = type;
    geometry->total_sectors = total;
    geometry->sectors_per_cluster = spc;
    geometry->reserved_sectors = reserved;
    geometry->root_sectors = root;
    geometry->fat_sectors = low;
    geometry->data_start = fixed + FAT_COUNT * low;
    geometry->clusters = clusters;
    return 0;
}

static unsigned
preferred_cluster_size(unsigned total, unsigned type)
{
    if (type == 16u) {
        if (total <= 262144u)
            return 4u;
        if (total <= 524288u)
            return 8u;
        if (total <= 1048576u)
            return 16u;
        if (total <= 2097152u)
            return 32u;
        if (total <= 4194304u)
            return 64u;
        return 128u;
    }
    if (total <= 532480u)
        return 1u;
    if (total <= 16777216u)
        return 8u;
    if (total <= 33554432u)
        return 16u;
    if (total <= 67108864u)
        return 32u;
    return 64u;
}

static int
select_geometry(unsigned total, const struct options *options,
    struct geometry *geometry)
{
    unsigned type;
    unsigned spc;
    int result;

    type = options->type;
    if (type == 0)
        type = total < 1048576u ? 16u : 32u;
    spc = options->sectors_per_cluster;
    if (spc != 0)
        return compute_geometry(total, type, spc, geometry);
    spc = preferred_cluster_size(total, type);
    for (;;) {
        result = compute_geometry(total, type, spc, geometry);
        if (result == 0)
            return 0;
        if (result == 1 && spc > 1u) {
            spc >>= 1;
            continue;
        }
        if (result == 2 && spc < 128u) {
            spc <<= 1;
            continue;
        }
        return result;
    }
}

static int
media_sectors(int fd, unsigned *sectors, unsigned *hidden)
{
    off_t current;
    off_t end;

    *hidden = 0;
#ifndef MKFS_FAT_HOST
    {
        struct diskpart part;

        if (ioctl(fd, DIOCGETSECTORS, sectors) == 0 && *sectors != 0) {
            if (ioctl(fd, DIOCGETPART, &part) == 0)
                *hidden = part.dp_offset;
            return 0;
        }
    }
#endif
    current = lseek(fd, (off_t)0, SEEK_CUR);
    end = lseek(fd, (off_t)0, SEEK_END);
    if (current >= 0)
        (void)lseek(fd, current, SEEK_SET);
    if (end < (off_t)SECTOR_SIZE || end % SECTOR_SIZE != 0 ||
        end / SECTOR_SIZE > (off_t)0xffffffffu)
        return -1;
    *sectors = (unsigned)(end / SECTOR_SIZE);
    return *sectors != 0 ? 0 : -1;
}

static int
write_sector(int fd, unsigned sector, const unsigned char *data)
{
    off_t offset;
    ssize_t count;

    offset = (off_t)sector * SECTOR_SIZE;
    if (lseek(fd, offset, SEEK_SET) != offset)
        return -1;
    count = write(fd, data, SECTOR_SIZE);
    if (count != SECTOR_SIZE) {
        if (count >= 0)
            errno = EIO;
        return -1;
    }
    return 0;
}

static void
make_boot_sector(unsigned char *boot, const struct geometry *geometry,
    const struct options *options, unsigned hidden)
{
    unsigned volume_id;
    unsigned char label[11];

    memset(boot, 0, SECTOR_SIZE);
    boot[0] = 0xebu;
    boot[1] = geometry->type == 16u ? 0x3cu : 0x58u;
    boot[2] = 0x90u;
    memcpy(boot + 3, "ReBSD   ", 8);
    put_le16(boot + 11, SECTOR_SIZE);
    boot[13] = (unsigned char)geometry->sectors_per_cluster;
    put_le16(boot + 14, geometry->reserved_sectors);
    boot[16] = FAT_COUNT;
    if (geometry->type == 16u)
        put_le16(boot + 17, FAT16_ROOT_ENTRIES);
    if (geometry->total_sectors < 0x10000u)
        put_le16(boot + 19, geometry->total_sectors);
    else
        put_le32(boot + 32, geometry->total_sectors);
    boot[21] = MEDIA_FIXED;
    if (geometry->type == 16u)
        put_le16(boot + 22, geometry->fat_sectors);
    put_le16(boot + 24, 63u);
    put_le16(boot + 26, 255u);
    put_le32(boot + 28, hidden);
    volume_id = options->volume_id_set ? options->volume_id :
        (unsigned)time((time_t *)0) ^ geometry->total_sectors;
    if (options->label_set)
        memcpy(label, options->label, sizeof(label));
    else
        memcpy(label, "NO NAME    ", sizeof(label));
    if (geometry->type == 16u) {
        boot[36] = 0x80u;
        boot[38] = 0x29u;
        put_le32(boot + 39, volume_id);
        memcpy(boot + 43, label, sizeof(label));
        memcpy(boot + 54, "FAT16   ", 8);
    } else {
        put_le32(boot + 36, geometry->fat_sectors);
        put_le32(boot + 44, FAT32_ROOT_CLUSTER);
        put_le16(boot + 48, FAT32_FSINFO_SECTOR);
        put_le16(boot + 50, FAT32_BACKUP_SECTOR);
        boot[64] = 0x80u;
        boot[66] = 0x29u;
        put_le32(boot + 67, volume_id);
        memcpy(boot + 71, label, sizeof(label));
        memcpy(boot + 82, "FAT32   ", 8);
    }
    boot[510] = 0x55u;
    boot[511] = 0xaau;
}

static void
make_fsinfo(unsigned char *data, const struct geometry *geometry)
{
    memset(data, 0, SECTOR_SIZE);
    put_le32(data, 0x41615252u);
    put_le32(data + 484, 0x61417272u);
    put_le32(data + 488, geometry->clusters - 1u);
    put_le32(data + 492, 3u);
    put_le32(data + 508, 0xaa550000u);
}

static void
make_first_fat_sector(unsigned char *data, const struct geometry *geometry)
{
    memset(data, 0, SECTOR_SIZE);
    if (geometry->type == 16u) {
        put_le16(data, 0xff00u | MEDIA_FIXED);
        put_le16(data + 2, 0xffffu);
    } else {
        put_le32(data, 0x0fffff00u | MEDIA_FIXED);
        put_le32(data + 4, 0x0fffffffu);
        put_le32(data + 8, 0x0fffffffu);
    }
}

static void
make_label_entry(unsigned char *data, const struct options *options)
{
    memset(data, 0, SECTOR_SIZE);
    if (!options->label_set)
        return;
    memcpy(data, options->label, 11);
    data[11] = 0x08u;
}

static int
format_volume(int fd, const struct geometry *geometry,
    const struct options *options, unsigned hidden)
{
    unsigned char data[SECTOR_SIZE];
    unsigned char boot[SECTOR_SIZE];
    unsigned fat;
    unsigned sector;
    unsigned start;

    memset(data, 0, sizeof(data));
    for (sector = 0; sector < geometry->reserved_sectors; ++sector)
        if (write_sector(fd, sector, data) < 0)
            return -1;
    make_boot_sector(boot, geometry, options, hidden);
    if (write_sector(fd, 0, boot) < 0)
        return -1;
    if (geometry->type == 32u) {
        make_fsinfo(data, geometry);
        if (write_sector(fd, FAT32_FSINFO_SECTOR, data) < 0 ||
            write_sector(fd, FAT32_BACKUP_SECTOR, boot) < 0 ||
            write_sector(fd, FAT32_BACKUP_SECTOR + FAT32_FSINFO_SECTOR,
            data) < 0)
            return -1;
    }
    for (fat = 0; fat < FAT_COUNT; ++fat) {
        start = geometry->reserved_sectors + fat * geometry->fat_sectors;
        make_first_fat_sector(data, geometry);
        if (write_sector(fd, start, data) < 0)
            return -1;
        memset(data, 0, sizeof(data));
        for (sector = 1; sector < geometry->fat_sectors; ++sector)
            if (write_sector(fd, start + sector, data) < 0)
                return -1;
    }
    make_label_entry(data, options);
    start = geometry->type == 16u ? geometry->reserved_sectors +
        FAT_COUNT * geometry->fat_sectors : geometry->data_start;
    if (write_sector(fd, start, data) < 0)
        return -1;
    memset(data, 0, sizeof(data));
    if (geometry->type == 16u) {
        for (sector = 1; sector < geometry->root_sectors; ++sector)
            if (write_sector(fd, start + sector, data) < 0)
                return -1;
    } else {
        for (sector = 1; sector < geometry->sectors_per_cluster; ++sector)
            if (write_sector(fd, start + sector, data) < 0)
                return -1;
    }
    if (fsync(fd) < 0)
        return -1;
#ifndef MKFS_FAT_HOST
    (void)ioctl(fd, DIOCFLUSH);
#endif
    return 0;
}

int
main(int argc, char **argv)
{
    struct options options;
    struct geometry geometry;
    unsigned sectors;
    unsigned hidden;
    unsigned value;
    int fd;
    int ch;
    int result;

    if (argv[0] != 0 && argv[0][0] != '\0')
        program_name = argv[0];
    memset(&options, 0, sizeof(options));
    while ((ch = getopt(argc, argv, "F:Ni:n:s:v")) != -1) {
        switch (ch) {
        case 'F':
            if (parse_unsigned(optarg, 32u, &value) < 0 ||
                (value != 16u && value != 32u))
                usage();
            options.type = value;
            break;
        case 'N':
            options.no_write = 1;
            break;
        case 'i':
            if (parse_unsigned(optarg, 0xffffffffu, &options.volume_id) < 0)
                usage();
            options.volume_id_set = 1;
            break;
        case 'n':
            if (make_label(optarg, options.label) < 0) {
                fprintf(stderr, "%s: invalid volume label\n", program_name);
                return 1;
            }
            options.label_set = 1;
            break;
        case 's':
            if (parse_unsigned(optarg, 128u, &value) < 0 ||
                !power_of_two(value))
                usage();
            options.sectors_per_cluster = value;
            break;
        case 'v':
            options.verbose = 1;
            break;
        default:
            usage();
        }
    }
    if (optind + 1 != argc)
        usage();
    fd = open(argv[optind], options.no_write ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: %s: %s\n", program_name, argv[optind],
            strerror(errno));
        return 1;
    }
    if (media_sectors(fd, &sectors, &hidden) < 0) {
        fprintf(stderr, "%s: cannot determine 512-byte sector count for %s\n",
            program_name, argv[optind]);
        close(fd);
        return 1;
    }
    result = select_geometry(sectors, &options, &geometry);
    if (result != 0) {
        fprintf(stderr,
            "%s: %s is too %s for FAT%u with the requested cluster size\n",
            program_name, argv[optind], result == 1 ? "small" : "large",
            options.type != 0 ? options.type :
            (sectors < 1048576u ? 16u : 32u));
        close(fd);
        return 1;
    }
    printf("%s: %u sectors, FAT%u, %u sectors/cluster, %u clusters\n",
        argv[optind], geometry.total_sectors, geometry.type,
        geometry.sectors_per_cluster, geometry.clusters);
    printf("%s: %u reserved sectors, %u sectors/FAT, %u FATs\n",
        argv[optind], geometry.reserved_sectors, geometry.fat_sectors,
        FAT_COUNT);
    if (options.verbose)
        printf("%s: data starts at sector %u, hidden sectors %u, MBR type 0x%02x\n",
            argv[optind], geometry.data_start, hidden,
            geometry.type == 32u ? 0x0cu :
            (geometry.total_sectors < 65536u ? 0x04u : 0x0eu));
    if (options.no_write) {
        close(fd);
        return 0;
    }
    if (format_volume(fd, &geometry, &options, hidden) < 0) {
        fprintf(stderr, "%s: formatting %s: %s\n", program_name,
            argv[optind], strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    printf("%s: filesystem created\n", argv[optind]);
    return 0;
}
