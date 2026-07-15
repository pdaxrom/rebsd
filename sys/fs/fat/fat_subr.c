/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <fs/fat/fat.h>

#define FAT12_CLUSTER_LIMIT         4085u
#define FAT16_CLUSTER_LIMIT         65525u
#define FAT16_EOC_MIN               0xfff8u
#define FAT16_BAD_CLUSTER           0xfff7u
#define FAT32_EOC_MIN               0x0ffffff8u
#define FAT32_BAD_CLUSTER           0x0ffffff7u
#define FAT32_CLUSTER_MASK          0x0fffffffu

static unsigned
fat_get_le16(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8);
}

static unsigned
fat_get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void
fat_put_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void
fat_put_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static void
fat_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static int
fat_power_of_two(unsigned value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

int
fat_volume_parse(struct fat_volume *volume, const unsigned char *boot,
    unsigned media_sectors)
{
    unsigned bytes_per_sector, sectors_per_cluster, reserved, fats;
    unsigned root_entries, total_sectors, fat_sectors, root_dir_sectors;
    unsigned overhead, data_sectors, clusters, fat_entries, root_cluster;
    unsigned active_fat, ext_flags;

    if (volume == 0)
        return FAT_PARSE_INVALID;
    fat_zero(volume, sizeof(*volume));
    if (boot == 0 || boot[510] != 0x55u || boot[511] != 0xaau)
        return FAT_PARSE_INVALID;

    bytes_per_sector = fat_get_le16(boot + 11);
    sectors_per_cluster = boot[13];
    reserved = fat_get_le16(boot + 14);
    fats = boot[16];
    root_entries = fat_get_le16(boot + 17);
    total_sectors = fat_get_le16(boot + 19);
    if (total_sectors == 0)
        total_sectors = fat_get_le32(boot + 32);
    fat_sectors = fat_get_le16(boot + 22);
    if (fat_sectors == 0)
        fat_sectors = fat_get_le32(boot + 36);

    if (bytes_per_sector != FAT_SECTOR_SIZE ||
        !fat_power_of_two(sectors_per_cluster) ||
        sectors_per_cluster > 128u || reserved == 0 || fats == 0 ||
        fats > 4u || total_sectors == 0 || fat_sectors == 0)
        return FAT_PARSE_INVALID;
    if (media_sectors != 0 && total_sectors > media_sectors)
        return FAT_PARSE_INVALID;
    if (root_entries > (0xffffffffu - (FAT_SECTOR_SIZE - 1u)) /
        FAT_DIRENT_SIZE)
        return FAT_PARSE_INVALID;
    root_dir_sectors = (root_entries * FAT_DIRENT_SIZE +
        FAT_SECTOR_SIZE - 1u) / FAT_SECTOR_SIZE;
    if (fat_sectors > (0xffffffffu - reserved) / fats)
        return FAT_PARSE_INVALID;
    overhead = reserved + fats * fat_sectors;
    if (root_dir_sectors > 0xffffffffu - overhead)
        return FAT_PARSE_INVALID;
    overhead += root_dir_sectors;
    if (overhead >= total_sectors)
        return FAT_PARSE_INVALID;
    data_sectors = total_sectors - overhead;
    clusters = data_sectors / sectors_per_cluster;
    if (clusters < FAT12_CLUSTER_LIMIT)
        return FAT_PARSE_UNSUPPORTED;

    ext_flags = 0;
    volume->fv_type = clusters < FAT16_CLUSTER_LIMIT ?
        FAT_TYPE_16 : FAT_TYPE_32;
    if (volume->fv_type == FAT_TYPE_16) {
        if (root_entries == 0 || fat_get_le16(boot + 22) == 0)
            return FAT_PARSE_INVALID;
        if (fat_sectors > 0xffffffffu / (FAT_SECTOR_SIZE / 2u))
            return FAT_PARSE_INVALID;
        root_cluster = 0;
        active_fat = 0;
        fat_entries = fat_sectors * (FAT_SECTOR_SIZE / 2u);
    } else {
        if (root_entries != 0 || fat_get_le32(boot + 36) == 0)
            return FAT_PARSE_INVALID;
        if (fat_get_le16(boot + 42) != 0)
            return FAT_PARSE_UNSUPPORTED;
        if (fat_sectors > 0xffffffffu / (FAT_SECTOR_SIZE / 4u))
            return FAT_PARSE_INVALID;
        ext_flags = fat_get_le16(boot + 40);
        active_fat = (ext_flags & 0x80u) != 0 ? ext_flags & 0x0fu : 0;
        if (active_fat >= fats)
            return FAT_PARSE_INVALID;
        root_cluster = fat_get_le32(boot + 44) & FAT32_CLUSTER_MASK;
        fat_entries = fat_sectors * (FAT_SECTOR_SIZE / 4u);
    }
    if (clusters > 0x0ffffff5u || fat_entries < clusters + 2u)
        return FAT_PARSE_INVALID;

    volume->fv_total_sectors = total_sectors;
    volume->fv_reserved_sectors = reserved;
    volume->fv_fat_start = reserved + active_fat * fat_sectors;
    volume->fv_fat_sectors = fat_sectors;
    volume->fv_data_start = overhead;
    volume->fv_root_dir_start = reserved + fats * fat_sectors;
    volume->fv_root_dir_sectors = root_dir_sectors;
    volume->fv_root_cluster = root_cluster;
    volume->fv_cluster_count = clusters;
    volume->fv_max_cluster = clusters + 1u;
    volume->fv_sectors_per_cluster = sectors_per_cluster;
    volume->fv_fat_count = fats;
    volume->fv_active_fat = active_fat;
    volume->fv_fat_mirrored = volume->fv_type == FAT_TYPE_16 ||
        (ext_flags & 0x80u) == 0;
    if (volume->fv_type == FAT_TYPE_32 &&
        !fat_cluster_valid(volume, root_cluster)) {
        fat_zero(volume, sizeof(*volume));
        return FAT_PARSE_INVALID;
    }
    return FAT_PARSE_OK;
}

int
fat_cluster_valid(const struct fat_volume *volume, unsigned cluster)
{
    return volume != 0 && cluster >= 2u &&
        cluster <= volume->fv_max_cluster;
}

unsigned
fat_cluster_first_sector(const struct fat_volume *volume, unsigned cluster)
{
    if (!fat_cluster_valid(volume, cluster))
        return 0xffffffffu;
    return volume->fv_data_start +
        (cluster - 2u) * volume->fv_sectors_per_cluster;
}

int
fat_fat_position(const struct fat_volume *volume, unsigned cluster,
    unsigned *sector, unsigned *offset)
{
    unsigned byte_offset, entry_size;

    if (!fat_cluster_valid(volume, cluster) || sector == 0 || offset == 0)
        return FAT_PARSE_INVALID;
    entry_size = volume->fv_type == FAT_TYPE_16 ? 2u : 4u;
    byte_offset = cluster * entry_size;
    *sector = volume->fv_fat_start + byte_offset / FAT_SECTOR_SIZE;
    *offset = byte_offset & (FAT_SECTOR_SIZE - 1u);
    if (*sector >= volume->fv_fat_start + volume->fv_fat_sectors)
        return FAT_PARSE_INVALID;
    return FAT_PARSE_OK;
}

unsigned
fat_fat_decode(const struct fat_volume *volume, const unsigned char *entry)
{
    if (volume == 0 || entry == 0)
        return 0;
    if (volume->fv_type == FAT_TYPE_16)
        return fat_get_le16(entry);
    return fat_get_le32(entry) & FAT32_CLUSTER_MASK;
}

void
fat_fat_encode(const struct fat_volume *volume, unsigned char *entry,
    unsigned cluster)
{
    unsigned value;

    if (volume == 0 || entry == 0)
        return;
    if (volume->fv_type == FAT_TYPE_16) {
        fat_put_le16(entry, cluster);
        return;
    }
    value = fat_get_le32(entry) & 0xf0000000u;
    value |= cluster & FAT32_CLUSTER_MASK;
    fat_put_le32(entry, value);
}

int
fat_cluster_is_eoc(const struct fat_volume *volume, unsigned cluster)
{
    if (volume == 0)
        return 0;
    return volume->fv_type == FAT_TYPE_16 ? cluster >= FAT16_EOC_MIN :
        cluster >= FAT32_EOC_MIN;
}

int
fat_cluster_is_bad(const struct fat_volume *volume, unsigned cluster)
{
    if (volume == 0)
        return 1;
    return volume->fv_type == FAT_TYPE_16 ? cluster == FAT16_BAD_CLUSTER :
        cluster == FAT32_BAD_CLUSTER;
}

void
fat_dirent_parse(struct fat_dirent *dirent, const unsigned char *entry)
{
    if (dirent == 0)
        return;
    fat_zero(dirent, sizeof(*dirent));
    if (entry == 0)
        return;
    dirent->fd_attr = entry[11];
    dirent->fd_ntres = entry[12];
    dirent->fd_create_time = fat_get_le16(entry + 14);
    dirent->fd_create_date = fat_get_le16(entry + 16);
    dirent->fd_access_date = fat_get_le16(entry + 18);
    dirent->fd_modify_time = fat_get_le16(entry + 22);
    dirent->fd_modify_date = fat_get_le16(entry + 24);
    dirent->fd_cluster = (fat_get_le16(entry + 20) << 16) |
        fat_get_le16(entry + 26);
    dirent->fd_size = fat_get_le32(entry + 28);
}

int
fat_dirent_is_visible(const unsigned char *entry)
{
    unsigned attr;

    if (entry == 0 || entry[0] == FAT_DIRENT_END ||
        entry[0] == FAT_DIRENT_DELETED)
        return 0;
    attr = entry[11];
    return attr != FAT_ATTR_LONG_NAME &&
        (attr & FAT_ATTR_VOLUME_ID) == 0;
}

unsigned
fat_lfn_checksum(const unsigned char *short_name)
{
    unsigned checksum, i;

    checksum = 0;
    if (short_name == 0)
        return checksum;
    for (i = 0; i < 11u; ++i) {
        checksum = ((checksum & 1u) ? 0x80u : 0u) +
            (checksum >> 1) + short_name[i];
        checksum &= 0xffu;
    }
    return checksum;
}

static unsigned char
fat_short_case(unsigned char ch, int lower)
{
    if (lower && ch >= 'A' && ch <= 'Z')
        return ch + ('a' - 'A');
    return ch;
}

int
fat_short_name(const unsigned char *entry, char *name, unsigned name_size)
{
    unsigned base_length, ext_length, length, i;
    int lower_base, lower_ext;

    if (entry == 0 || name == 0 || name_size == 0)
        return FAT_PARSE_INVALID;
    base_length = 8u;
    while (base_length != 0 && entry[base_length - 1u] == ' ')
        --base_length;
    ext_length = 3u;
    while (ext_length != 0 && entry[8u + ext_length - 1u] == ' ')
        --ext_length;
    if (base_length == 0)
        return FAT_PARSE_INVALID;
    length = base_length + (ext_length != 0 ? 1u + ext_length : 0u);
    if (length + 1u > name_size)
        return FAT_PARSE_INVALID;
    lower_base = (entry[12] & 0x08u) != 0;
    lower_ext = (entry[12] & 0x10u) != 0;
    for (i = 0; i < base_length; ++i) {
        unsigned char ch = entry[i];

        if (i == 0 && ch == 0x05u)
            ch = FAT_DIRENT_DELETED;
        name[i] = fat_short_case(ch, lower_base);
    }
    if (ext_length != 0) {
        name[base_length] = '.';
        for (i = 0; i < ext_length; ++i)
            name[base_length + 1u + i] =
                fat_short_case(entry[8u + i], lower_ext);
    }
    name[length] = '\0';
    return (int)length;
}

static int
fat_short_char(unsigned char ch)
{
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
        return 1;
    switch (ch) {
    case '$':
    case '%':
    case '\'':
    case '-':
    case '_':
    case '@':
    case '~':
    case '`':
    case '!':
    case '(':
    case ')':
    case '{':
    case '}':
    case '^':
    case '#':
    case '&':
        return 1;
    default:
        return 0;
    }
}

int
fat_short_name_encode(const char *name, unsigned length,
    unsigned char *short_name)
{
    unsigned base_length;
    unsigned ext_length;
    unsigned dot;
    unsigned i;

    if (name == 0 || short_name == 0 || length == 0)
        return FAT_PARSE_INVALID;
    dot = length;
    for (i = 0; i < length; ++i) {
        if (name[i] == '.') {
            if (dot != length)
                return FAT_PARSE_UNSUPPORTED;
            dot = i;
        }
    }
    base_length = dot;
    ext_length = dot == length ? 0 : length - dot - 1u;
    if (base_length == 0 || base_length > 8u || ext_length > 3u ||
        (dot != length && ext_length == 0))
        return FAT_PARSE_UNSUPPORTED;
    for (i = 0; i < 11u; ++i)
        short_name[i] = ' ';
    for (i = 0; i < base_length; ++i) {
        unsigned char ch;

        ch = (unsigned char)name[i];
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (!fat_short_char(ch))
            return FAT_PARSE_UNSUPPORTED;
        short_name[i] = ch;
    }
    for (i = 0; i < ext_length; ++i) {
        unsigned char ch;

        ch = (unsigned char)name[dot + 1u + i];
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (!fat_short_char(ch))
            return FAT_PARSE_UNSUPPORTED;
        short_name[8u + i] = ch;
    }
    return FAT_PARSE_OK;
}

void
fat_dirent_encode(unsigned char *entry, const unsigned char *short_name,
    unsigned attr, unsigned cluster, unsigned size)
{
    unsigned i;

    if (entry == 0 || short_name == 0)
        return;
    fat_zero(entry, FAT_DIRENT_SIZE);
    for (i = 0; i < 11u; ++i)
        entry[i] = short_name[i];
    entry[11] = (unsigned char)attr;
    fat_put_le16(entry + 20, cluster >> 16);
    fat_put_le16(entry + 26, cluster);
    fat_put_le32(entry + 28, size);
}

void
fat_dirent_set_cluster_size(unsigned char *entry, unsigned cluster,
    unsigned size)
{
    if (entry == 0)
        return;
    fat_put_le16(entry + 20, cluster >> 16);
    fat_put_le16(entry + 26, cluster);
    fat_put_le32(entry + 28, size);
}

static unsigned char
fat_ascii_fold(unsigned char ch)
{
    if (ch >= 'a' && ch <= 'z')
        return ch - ('a' - 'A');
    return ch;
}

int
fat_ascii_name_equal(const char *left, unsigned left_length,
    const char *right, unsigned right_length)
{
    unsigned i;

    if (left == 0 || right == 0 || left_length != right_length)
        return 0;
    for (i = 0; i < left_length; ++i)
        if (fat_ascii_fold((unsigned char)left[i]) !=
            fat_ascii_fold((unsigned char)right[i]))
            return 0;
    return 1;
}
