/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * Small transport-independent read-only FAT16/FAT32 reader.
 *
 * This is used by early ports before the complete buffer cache, inode and
 * mount infrastructure is available.  Sector numbers passed to fr_read are
 * relative to the start of the FAT volume.
 */

#include <sys/errno.h>

#include <fs/fat/fat.h>

#define FAT_RO_SHORT_NAME_SIZE      13u

static void
fat_ro_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static int
fat_ro_sector(struct fat_ro *reader, unsigned sector, unsigned char *data)
{
    if (reader == 0 || reader->fr_read == 0 || data == 0 ||
        sector >= reader->fr_volume.fv_total_sectors)
        return EIO;
    return (*reader->fr_read)(reader->fr_arg, sector, data);
}

static unsigned
fat_ro_entry_cluster(const struct fat_ro *reader,
    const struct fat_dirent *dirent)
{
    if (reader->fr_volume.fv_type == FAT_TYPE_16)
        return dirent->fd_cluster & 0xffffu;
    return dirent->fd_cluster & 0x0fffffffu;
}

static int
fat_ro_next_cluster(struct fat_ro *reader, unsigned cluster,
    unsigned *nextp)
{
    unsigned char sector_data[FAT_SECTOR_SIZE];
    unsigned sector;
    unsigned offset;
    unsigned next;
    int error;

    if (nextp == 0 ||
        fat_fat_position(&reader->fr_volume, cluster, &sector, &offset) !=
        FAT_PARSE_OK)
        return EIO;
    error = fat_ro_sector(reader, sector, sector_data);
    if (error != 0)
        return error;
    next = fat_fat_decode(&reader->fr_volume, sector_data + offset);
    if (fat_cluster_is_eoc(&reader->fr_volume, next)) {
        *nextp = 0;
        return 0;
    }
    if (next == FAT_CLUSTER_FREE ||
        fat_cluster_is_bad(&reader->fr_volume, next) ||
        !fat_cluster_valid(&reader->fr_volume, next))
        return EIO;
    *nextp = next;
    return 0;
}

static int
fat_ro_scan_sector(struct fat_ro *reader, unsigned sector,
    const char *wanted, unsigned wanted_length, struct fat_ro_node *node,
    int *endedp)
{
    unsigned char data[FAT_SECTOR_SIZE];
    const unsigned char *entry;
    struct fat_dirent dirent;
    char name[FAT_RO_SHORT_NAME_SIZE];
    unsigned cluster;
    unsigned slot;
    int length;
    int error;

    error = fat_ro_sector(reader, sector, data);
    if (error != 0)
        return error;
    for (slot = 0; slot < FAT_DIRENTS_PER_SECTOR; ++slot) {
        entry = data + slot * FAT_DIRENT_SIZE;
        if (entry[0] == FAT_DIRENT_END) {
            *endedp = 1;
            return ENOENT;
        }
        if (!fat_dirent_is_visible(entry))
            continue;
        length = fat_short_name(entry, name, sizeof(name));
        if (length < 0 || !fat_ascii_name_equal(wanted, wanted_length,
            name, (unsigned)length))
            continue;
        fat_dirent_parse(&dirent, entry);
        cluster = fat_ro_entry_cluster(reader, &dirent);
        if ((dirent.fd_attr & FAT_ATTR_DIRECTORY) != 0) {
            if (!fat_cluster_valid(&reader->fr_volume, cluster))
                return EIO;
        } else if (dirent.fd_size != 0 &&
            !fat_cluster_valid(&reader->fr_volume, cluster))
            return EIO;
        node->fn_attr = dirent.fd_attr;
        node->fn_cluster = cluster;
        node->fn_size = dirent.fd_size;
        return 0;
    }
    return ENOENT;
}

static int
fat_ro_scan_directory(struct fat_ro *reader,
    const struct fat_ro_node *directory, const char *wanted,
    unsigned wanted_length, struct fat_ro_node *node)
{
    unsigned cluster;
    unsigned sector;
    unsigned next;
    unsigned visited;
    unsigned index;
    int ended;
    int error;

    ended = 0;
    if (reader->fr_volume.fv_type == FAT_TYPE_16 &&
        directory->fn_cluster == 0) {
        for (index = 0;
            index < reader->fr_volume.fv_root_dir_sectors; ++index) {
            error = fat_ro_scan_sector(reader,
                reader->fr_volume.fv_root_dir_start + index,
                wanted, wanted_length, node, &ended);
            if (error == 0 || error != ENOENT || ended)
                return error;
        }
        return ENOENT;
    }

    cluster = directory->fn_cluster;
    if (!fat_cluster_valid(&reader->fr_volume, cluster))
        return EIO;
    visited = 0;
    while (cluster != 0) {
        if (++visited > reader->fr_volume.fv_cluster_count)
            return EIO;
        sector = fat_cluster_first_sector(&reader->fr_volume, cluster);
        for (index = 0;
            index < reader->fr_volume.fv_sectors_per_cluster; ++index) {
            error = fat_ro_scan_sector(reader, sector + index,
                wanted, wanted_length, node, &ended);
            if (error == 0 || error != ENOENT || ended)
                return error;
        }
        error = fat_ro_next_cluster(reader, cluster, &next);
        if (error != 0)
            return error;
        cluster = next;
    }
    return ENOENT;
}

int
fat_ro_mount(struct fat_ro *reader, unsigned media_sectors,
    fat_sector_read_fn read_sector, void *arg)
{
    unsigned char boot[FAT_SECTOR_SIZE];
    int parsed;
    int error;

    if (reader == 0 || read_sector == 0 || media_sectors == 0)
        return EINVAL;
    fat_ro_zero(reader, sizeof(*reader));
    reader->fr_volume.fv_total_sectors = media_sectors;
    reader->fr_read = read_sector;
    reader->fr_arg = arg;
    error = fat_ro_sector(reader, 0, boot);
    if (error != 0) {
        fat_ro_zero(reader, sizeof(*reader));
        return error;
    }
    parsed = fat_volume_parse(&reader->fr_volume, boot, media_sectors);
    if (parsed != FAT_PARSE_OK) {
        fat_ro_zero(reader, sizeof(*reader));
        return parsed == FAT_PARSE_UNSUPPORTED ? EOPNOTSUPP : EINVAL;
    }
    return 0;
}

int
fat_ro_lookup(struct fat_ro *reader, const char *path,
    struct fat_ro_node *node)
{
    struct fat_ro_node current;
    struct fat_ro_node found;
    const char *component;
    unsigned length;
    int error;

    if (reader == 0 || reader->fr_read == 0 || path == 0 || node == 0)
        return EINVAL;
    fat_ro_zero(&current, sizeof(current));
    current.fn_attr = FAT_ATTR_DIRECTORY;
    current.fn_cluster = reader->fr_volume.fv_type == FAT_TYPE_32 ?
        reader->fr_volume.fv_root_cluster : 0;

    while (*path == '/')
        ++path;
    if (*path == '\0') {
        *node = current;
        return 0;
    }
    for (;;) {
        component = path;
        length = 0;
        while (*path != '\0' && *path != '/') {
            ++path;
            ++length;
        }
        if (length == 0 || length >= FAT_RO_SHORT_NAME_SIZE)
            return ENAMETOOLONG;
        if ((current.fn_attr & FAT_ATTR_DIRECTORY) == 0)
            return ENOTDIR;
        error = fat_ro_scan_directory(reader, &current, component, length,
            &found);
        if (error != 0)
            return error;
        current = found;
        if (*path == '\0') {
            *node = current;
            return 0;
        }
        while (*path == '/')
            ++path;
        if (*path == '\0') {
            if ((current.fn_attr & FAT_ATTR_DIRECTORY) == 0)
                return ENOTDIR;
            *node = current;
            return 0;
        }
    }
}

int
fat_ro_read(struct fat_ro *reader, const struct fat_ro_node *node,
    unsigned offset, void *data_arg, unsigned length, unsigned *readp)
{
    unsigned char sector_data[FAT_SECTOR_SIZE];
    unsigned char *data;
    unsigned cluster;
    unsigned cluster_bytes;
    unsigned within;
    unsigned skip;
    unsigned visited;
    unsigned sector;
    unsigned sector_offset;
    unsigned available;
    unsigned count;
    unsigned next;
    unsigned index;
    int error;

    if (readp != 0)
        *readp = 0;
    if (reader == 0 || reader->fr_read == 0 || node == 0 ||
        (length != 0 && data_arg == 0))
        return EINVAL;
    if ((node->fn_attr & FAT_ATTR_DIRECTORY) != 0)
        return EISDIR;
    if (length == 0 || offset >= node->fn_size)
        return 0;
    if (!fat_cluster_valid(&reader->fr_volume, node->fn_cluster))
        return EIO;

    available = node->fn_size - offset;
    if (length > available)
        length = available;
    data = (unsigned char *)data_arg;
    cluster_bytes = reader->fr_volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    skip = offset / cluster_bytes;
    within = offset % cluster_bytes;
    cluster = node->fn_cluster;
    visited = 0;
    while (skip-- != 0) {
        if (++visited > reader->fr_volume.fv_cluster_count)
            return EIO;
        error = fat_ro_next_cluster(reader, cluster, &next);
        if (error != 0 || next == 0)
            return EIO;
        cluster = next;
    }

    count = 0;
    while (count < length) {
        sector = fat_cluster_first_sector(&reader->fr_volume, cluster) +
            within / FAT_SECTOR_SIZE;
        sector_offset = within & (FAT_SECTOR_SIZE - 1u);
        available = FAT_SECTOR_SIZE - sector_offset;
        if (available > length - count)
            available = length - count;
        error = fat_ro_sector(reader, sector, sector_data);
        if (error != 0)
            return error;
        for (index = 0; index < available; ++index)
            data[count + index] = sector_data[sector_offset + index];
        count += available;
        within += available;
        if (within == cluster_bytes && count < length) {
            if (++visited > reader->fr_volume.fv_cluster_count)
                return EIO;
            error = fat_ro_next_cluster(reader, cluster, &next);
            if (error != 0 || next == 0)
                return EIO;
            cluster = next;
            within = 0;
        }
    }
    if (readp != 0)
        *readp = count;
    return 0;
}
