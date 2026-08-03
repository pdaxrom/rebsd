/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* FAT16/FAT32 VFS glue.  The backing store is any block device. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/todr.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/dir.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <fs/fat/fat.h>

#define FAT_FILE_INO_BASE           16u
#define FAT_DIR_INO_BASE            0xf0000000u
#define FAT_DIR_CLUSTER_MASK        0x0fffffffu
#define FAT_MAX_FILE_SECTORS        \
    ((FAT_DIR_INO_BASE - FAT_FILE_INO_BASE) / FAT_DIRENTS_PER_SECTOR)
#define FAT_LFN_CHARS               MAXNAMLEN
#define FAT_INSERT_END_LAST         (-1)
#define FAT_SCAN_FOUND              (-2)

static const unsigned char fat_dot_name[] = ".          ";
static const unsigned char fat_dotdot_name[] = "..         ";

struct fat_mount {
    int fm_used;
    int fm_read_only;
    struct mount *fm_mount;
    dev_t fm_dev;
    unsigned fm_media_sectors;
    unsigned fm_next_free;
    unsigned fm_free_clusters;
    struct fat_volume fm_volume;
};

struct fat_lfn {
    u_short fl_chars[FAT_LFN_CHARS + 1];
    unsigned fl_checksum;
    unsigned fl_expected;
    int fl_valid;
};

typedef int (*fat_scan_fn)(struct fat_mount *, const unsigned char *,
    unsigned, unsigned, const char *, unsigned, void *);

struct fat_dir_build {
    char *fb_block;
    off_t fb_block_base;
    off_t fb_offset;
    off_t fb_last_offset;
};

struct fat_entry_find {
    const unsigned char *ff_name;
    unsigned ff_cluster;
    unsigned ff_sector;
    unsigned ff_slot;
    int ff_found;
};

static struct fat_mount fat_mounts[NMOUNT];

static void
fat_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

void
fatattach(int unit)
{
    (void)unit;
    fat_zero(fat_mounts, sizeof(fat_mounts));
    printf("fat: FAT16/FAT32 filesystem ready\n");
}

static struct fat_mount *
fat_mount_from_inode(struct inode *ip)
{
    struct mount *mp;

    if (ip == 0 || ip->i_fs == 0)
        return 0;
    mp = (struct mount *)((int)ip->i_fs - offsetof(struct mount, m_filsys));
    return (struct fat_mount *)mp->m_data;
}

static int
fat_sector_get(struct fat_mount *fmp, unsigned sector, struct buf **bpp,
    unsigned char **datap)
{
    struct buf *bp;
    unsigned offset;
    unsigned available;

    if (fmp == 0 || bpp == 0 || datap == 0 ||
        sector >= fmp->fm_volume.fv_total_sectors)
        return EIO;
    bp = bread(fmp->fm_dev, (daddr_t)(sector >> 1));
    if ((bp->b_flags & B_ERROR) != 0) {
        brelse(bp);
        return EIO;
    }
    if (bp->b_resid > bp->b_bcount) {
        brelse(bp);
        return EIO;
    }
    offset = (sector & 1u) * FAT_SECTOR_SIZE;
    available = bp->b_bcount - bp->b_resid;
    if (available < offset + FAT_SECTOR_SIZE) {
        brelse(bp);
        return EIO;
    }
    *bpp = bp;
    *datap = (unsigned char *)bp->b_addr + offset;
    return 0;
}

static int
fat_buffer_write(struct fat_mount *fmp, struct buf *bp)
{
    int error;

    if (bp == 0)
        return EINVAL;
    if (fmp == 0 || fmp->fm_read_only) {
        brelse(bp);
        return EROFS;
    }
    bwrite(bp);
    error = geterror(bp);
    return error;
}

static int
fat_cluster_read(struct fat_mount *fmp, unsigned cluster, unsigned *valuep)
{
    struct buf *bp;
    unsigned char *data;
    unsigned sector;
    unsigned offset;
    int error;

    if (valuep == 0 || fat_fat_position(&fmp->fm_volume, cluster,
        &sector, &offset) != FAT_PARSE_OK)
        return EIO;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    *valuep = fat_fat_decode(&fmp->fm_volume, data + offset);
    brelse(bp);
    return 0;
}

static int
fat_count_free_clusters(struct fat_mount *fmp)
{
    struct buf *bp;
    unsigned char *data;
    unsigned entries_per_sector;
    unsigned entry_size;
    unsigned first;
    unsigned last;
    unsigned cluster;
    unsigned sector;
    unsigned i;
    int error;

    entry_size = fmp->fm_volume.fv_type == FAT_TYPE_16 ? 2u : 4u;
    entries_per_sector = FAT_SECTOR_SIZE / entry_size;
    fmp->fm_free_clusters = 0;
    fmp->fm_next_free = 2u;
    for (i = 0; i < fmp->fm_volume.fv_fat_sectors; ++i) {
        first = i * entries_per_sector;
        if (first > fmp->fm_volume.fv_max_cluster)
            break;
        last = first + entries_per_sector;
        if (last > fmp->fm_volume.fv_max_cluster + 1u)
            last = fmp->fm_volume.fv_max_cluster + 1u;
        if (first < 2u)
            first = 2u;
        if (first >= last)
            continue;
        sector = fmp->fm_volume.fv_fat_start + i;
        error = fat_sector_get(fmp, sector, &bp, &data);
        if (error)
            return error;
        for (cluster = first; cluster < last; ++cluster)
            if (fat_fat_decode(&fmp->fm_volume,
                data + (cluster % entries_per_sector) * entry_size) ==
                FAT_CLUSTER_FREE) {
                if (fmp->fm_free_clusters == 0)
                    fmp->fm_next_free = cluster;
                ++fmp->fm_free_clusters;
            }
        brelse(bp);
    }
    return 0;
}

static int
fat_cluster_write_copy(struct fat_mount *fmp, unsigned fat_index,
    unsigned cluster, unsigned value)
{
    struct buf *bp;
    unsigned char *data;
    unsigned byte_offset;
    unsigned entry_size;
    unsigned sector;
    unsigned offset;
    int error;

    entry_size = fmp->fm_volume.fv_type == FAT_TYPE_16 ? 2u : 4u;
    byte_offset = cluster * entry_size;
    sector = fmp->fm_volume.fv_reserved_sectors +
        fat_index * fmp->fm_volume.fv_fat_sectors +
        byte_offset / FAT_SECTOR_SIZE;
    offset = byte_offset & (FAT_SECTOR_SIZE - 1u);
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    fat_fat_encode(&fmp->fm_volume, data + offset, value);
    return fat_buffer_write(fmp, bp);
}

static int
fat_cluster_write(struct fat_mount *fmp, unsigned cluster, unsigned value)
{
    unsigned first;
    unsigned count;
    unsigned i;
    int error;

    if (fmp == 0 || fmp->fm_read_only ||
        !fat_cluster_valid(&fmp->fm_volume, cluster))
        return fmp != 0 && fmp->fm_read_only ? EROFS : EINVAL;
    if (fmp->fm_volume.fv_fat_mirrored) {
        first = 0;
        count = fmp->fm_volume.fv_fat_count;
    } else {
        first = fmp->fm_volume.fv_active_fat;
        count = 1;
    }
    for (i = 0; i < count; ++i) {
        error = fat_cluster_write_copy(fmp, first + i, cluster, value);
        if (error)
            return error;
    }
    return 0;
}

static int
fat_cluster_release(struct fat_mount *fmp, unsigned cluster)
{
    int error;

    error = fat_cluster_write(fmp, cluster, FAT_CLUSTER_FREE);
    if (error)
        return error;
    if (fmp->fm_free_clusters < fmp->fm_volume.fv_cluster_count)
        ++fmp->fm_free_clusters;
    if (cluster < fmp->fm_next_free ||
        !fat_cluster_valid(&fmp->fm_volume, fmp->fm_next_free))
        fmp->fm_next_free = cluster;
    return 0;
}

static unsigned
fat_eoc_value(const struct fat_volume *volume)
{
    return volume->fv_type == FAT_TYPE_16 ? 0xffffu : 0x0fffffffu;
}

static int
fat_zero_cluster(struct fat_mount *fmp, unsigned cluster)
{
    struct buf *bp;
    unsigned char *data;
    unsigned sector;
    unsigned i;
    int error;

    sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
    if (sector == 0xffffffffu)
        return EIO;
    for (i = 0; i < fmp->fm_volume.fv_sectors_per_cluster; ++i) {
        error = fat_sector_get(fmp, sector + i, &bp, &data);
        if (error)
            return error;
        fat_zero(data, FAT_SECTOR_SIZE);
        error = fat_buffer_write(fmp, bp);
        if (error)
            return error;
    }
    return 0;
}

static int
fat_cluster_alloc(struct fat_mount *fmp, int clear, unsigned *clusterp)
{
    unsigned cluster;
    unsigned value;
    unsigned checked;
    int error;

    if (fmp == 0 || clusterp == 0)
        return EINVAL;
    if (fmp->fm_read_only)
        return EROFS;
    cluster = fat_cluster_valid(&fmp->fm_volume, fmp->fm_next_free) ?
        fmp->fm_next_free : 2u;
    for (checked = 0; checked < fmp->fm_volume.fv_cluster_count;
        ++checked) {
        error = fat_cluster_read(fmp, cluster, &value);
        if (error)
            return error;
        if (value == FAT_CLUSTER_FREE) {
            error = fat_cluster_write(fmp, cluster,
                fat_eoc_value(&fmp->fm_volume));
            if (error)
                return error;
            if (fmp->fm_free_clusters != 0)
                --fmp->fm_free_clusters;
            if (clear) {
                error = fat_zero_cluster(fmp, cluster);
                if (error) {
                    (void)fat_cluster_release(fmp, cluster);
                    return error;
                }
            }
            fmp->fm_next_free = cluster == fmp->fm_volume.fv_max_cluster ?
                2u : cluster + 1u;
            *clusterp = cluster;
            return 0;
        }
        cluster = cluster == fmp->fm_volume.fv_max_cluster ?
            2u : cluster + 1u;
    }
    return ENOSPC;
}

static int
fat_entry_read(struct fat_mount *fmp, unsigned sector, unsigned slot,
    unsigned char *entry)
{
    struct buf *bp;
    unsigned char *data;
    int error;

    if (slot >= FAT_DIRENTS_PER_SECTOR || entry == 0)
        return EINVAL;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    bcopy(data + slot * FAT_DIRENT_SIZE, entry, FAT_DIRENT_SIZE);
    brelse(bp);
    return 0;
}

static int
fat_next_cluster(struct fat_mount *fmp, unsigned cluster, unsigned *nextp)
{
    struct buf *bp;
    unsigned char *data;
    unsigned sector;
    unsigned offset;
    unsigned next;
    int error;

    if (nextp == 0 || fat_fat_position(&fmp->fm_volume, cluster,
        &sector, &offset) != FAT_PARSE_OK)
        return EIO;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    next = fat_fat_decode(&fmp->fm_volume, data + offset);
    brelse(bp);
    if (fat_cluster_is_eoc(&fmp->fm_volume, next)) {
        *nextp = 0;
        return 0;
    }
    if (fat_cluster_is_bad(&fmp->fm_volume, next) ||
        !fat_cluster_valid(&fmp->fm_volume, next))
        return EIO;
    *nextp = next;
    return 0;
}

static void
fat_lfn_reset(struct fat_lfn *lfn)
{
    fat_zero(lfn, sizeof(*lfn));
}

static unsigned
fat_get_le16(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8);
}

static time_t
fat_timestamp(unsigned date, unsigned clock)
{
    struct clock_ymdhms dt;
    time_t timestamp;
    time_t seconds;
    time_t adjustment;
    int error;

    dt.dt_year = 1980u + ((date >> 9) & 0x7fu);
    dt.dt_mon = (date >> 5) & 0x0fu;
    dt.dt_day = date & 0x1fu;
    dt.dt_wday = 0;
    dt.dt_hour = (clock >> 11) & 0x1fu;
    dt.dt_min = (clock >> 5) & 0x3fu;
    dt.dt_sec = (clock & 0x1fu) * 2u;
    error = clock_ymdhms_to_secs(&dt, &timestamp);
    if (error != 0)
        return 0;
    seconds = timestamp;
    if (tz.tz_minuteswest < -1440 || tz.tz_minuteswest > 1440)
        adjustment = 0;
    else
        adjustment = (time_t)tz.tz_minuteswest * 60;
    if (adjustment < 0 && seconds < -adjustment)
        return 0;
    return seconds + adjustment;
}

static void
fat_lfn_store(struct fat_lfn *lfn, const unsigned char *entry,
    unsigned ordinal)
{
    static const unsigned char offsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    unsigned base;
    unsigned value;
    unsigned position;
    unsigned i;

    base = (ordinal - 1u) * 13u;
    for (i = 0; i < 13u; ++i) {
        value = fat_get_le16(entry + offsets[i]);
        position = base + i;
        if (position <= FAT_LFN_CHARS)
            lfn->fl_chars[position] = (u_short)value;
        else if (value != 0 && value != 0xffffu)
            lfn->fl_valid = 0;
    }
}

static void
fat_lfn_add(struct fat_lfn *lfn, const unsigned char *entry)
{
    unsigned sequence;
    unsigned ordinal;

    sequence = entry[0];
    ordinal = sequence & 0x1fu;
    if ((sequence & 0x80u) != 0) {
        lfn->fl_valid = 0;
        return;
    }
    if ((sequence & 0x40u) != 0) {
        fat_lfn_reset(lfn);
        lfn->fl_valid = ordinal != 0 &&
            ordinal <= (FAT_LFN_CHARS + 12u) / 13u;
        lfn->fl_checksum = entry[13];
        lfn->fl_expected = ordinal;
    }
    if (!lfn->fl_valid || ordinal == 0 ||
        ordinal != lfn->fl_expected || entry[12] != 0 ||
        entry[13] != lfn->fl_checksum || entry[26] != 0 || entry[27] != 0) {
        lfn->fl_valid = 0;
        return;
    }
    fat_lfn_store(lfn, entry, ordinal);
    lfn->fl_expected = ordinal - 1u;
}

static int
fat_utf8_put(char *name, unsigned *lengthp, unsigned value)
{
    unsigned length;

    length = *lengthp;
    if (value < 0x80u) {
        if (length + 1u > MAXNAMLEN)
            return EINVAL;
        name[length++] = (char)value;
    } else if (value < 0x800u) {
        if (length + 2u > MAXNAMLEN)
            return EINVAL;
        name[length++] = (char)(0xc0u | (value >> 6));
        name[length++] = (char)(0x80u | (value & 0x3fu));
    } else {
        if (value >= 0xd800u && value <= 0xdfffu)
            return EINVAL;
        if (length + 3u > MAXNAMLEN)
            return EINVAL;
        name[length++] = (char)(0xe0u | (value >> 12));
        name[length++] = (char)(0x80u | ((value >> 6) & 0x3fu));
        name[length++] = (char)(0x80u | (value & 0x3fu));
    }
    *lengthp = length;
    return 0;
}

static int
fat_lfn_name(struct fat_lfn *lfn, const unsigned char *short_entry,
    char *name, unsigned *lengthp)
{
    unsigned length;
    unsigned value;
    unsigned i;

    if (!lfn->fl_valid || lfn->fl_expected != 0 ||
        lfn->fl_checksum != fat_lfn_checksum(short_entry))
        return EINVAL;
    length = 0;
    for (i = 0; i <= FAT_LFN_CHARS; ++i) {
        value = lfn->fl_chars[i];
        if (value == 0 || value == 0xffffu)
            break;
        if (fat_utf8_put(name, &length, value))
            return EINVAL;
    }
    if (length == 0)
        return EINVAL;
    name[length] = '\0';
    *lengthp = length;
    return 0;
}

static int
fat_scan_sector(struct fat_mount *fmp, unsigned sector,
    struct fat_lfn *lfn, fat_scan_fn callback, void *arg, int *endedp)
{
    struct buf *bp;
    unsigned char *data;
    const unsigned char *entry;
    char name[MAXNAMLEN + 1];
    unsigned length;
    unsigned slot;
    int error;

    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    for (slot = 0; slot < FAT_DIRENTS_PER_SECTOR; ++slot) {
        entry = data + slot * FAT_DIRENT_SIZE;
        if (entry[0] == FAT_DIRENT_END) {
            *endedp = 1;
            break;
        }
        if (entry[0] == FAT_DIRENT_DELETED) {
            fat_lfn_reset(lfn);
            continue;
        }
        if (entry[11] == FAT_ATTR_LONG_NAME) {
            fat_lfn_add(lfn, entry);
            continue;
        }
        if (!fat_dirent_is_visible(entry)) {
            fat_lfn_reset(lfn);
            continue;
        }
        if (fat_lfn_name(lfn, entry, name, &length) != 0) {
            error = fat_short_name(entry, name, sizeof(name));
            if (error < 0) {
                fat_lfn_reset(lfn);
                continue;
            }
            length = (unsigned)error;
        }
        error = (*callback)(fmp, entry, sector, slot, name, length, arg);
        fat_lfn_reset(lfn);
        if (error)
            break;
    }
    brelse(bp);
    return error;
}

static int
fat_scan_directory(struct fat_mount *fmp, ino_t ino, fat_scan_fn callback,
    void *arg)
{
    struct fat_lfn lfn;
    unsigned cluster;
    unsigned sector;
    unsigned next;
    unsigned visited;
    unsigned i;
    int ended;
    int error;

    fat_lfn_reset(&lfn);
    ended = 0;
    if (ino == ROOTINO && fmp->fm_volume.fv_type == FAT_TYPE_16) {
        for (i = 0; i < fmp->fm_volume.fv_root_dir_sectors; ++i) {
            error = fat_scan_sector(fmp,
                fmp->fm_volume.fv_root_dir_start + i, &lfn,
                callback, arg, &ended);
            if (error || ended)
                return error;
        }
        return 0;
    }

    cluster = ino == ROOTINO ? fmp->fm_volume.fv_root_cluster :
        ((unsigned)ino & FAT_DIR_CLUSTER_MASK);
    if (!fat_cluster_valid(&fmp->fm_volume, cluster))
        return EIO;
    visited = 0;
    while (cluster != 0) {
        if (++visited > fmp->fm_volume.fv_cluster_count)
            return EIO;
        sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
        for (i = 0; i < fmp->fm_volume.fv_sectors_per_cluster; ++i) {
            error = fat_scan_sector(fmp, sector + i, &lfn,
                callback, arg, &ended);
            if (error || ended)
                return error;
        }
        error = fat_next_cluster(fmp, cluster, &next);
        if (error)
            return error;
        cluster = next;
    }
    return 0;
}

static ino_t
fat_directory_ino(unsigned cluster)
{
    return (ino_t)(FAT_DIR_INO_BASE | (cluster & FAT_DIR_CLUSTER_MASK));
}

static unsigned
fat_dirent_cluster(struct fat_mount *fmp, const struct fat_dirent *dirent)
{
    if (fmp->fm_volume.fv_type == FAT_TYPE_16)
        return dirent->fd_cluster & 0xffffu;
    return dirent->fd_cluster & FAT_DIR_CLUSTER_MASK;
}

static int
fat_file_ino(unsigned sector, unsigned slot, ino_t *inop)
{
    unsigned index;

    if (slot >= FAT_DIRENTS_PER_SECTOR || sector >= FAT_MAX_FILE_SECTORS)
        return EFBIG;
    index = sector * FAT_DIRENTS_PER_SECTOR + slot;
    *inop = (ino_t)(FAT_FILE_INO_BASE + index);
    if (*inop >= FAT_DIR_INO_BASE)
        return EFBIG;
    return 0;
}

static int
fat_file_location(ino_t ino, unsigned *sectorp, unsigned *slotp)
{
    unsigned index;

    if (ino < FAT_FILE_INO_BASE || ino >= FAT_DIR_INO_BASE ||
        sectorp == 0 || slotp == 0)
        return EINVAL;
    index = (unsigned)ino - FAT_FILE_INO_BASE;
    *sectorp = index / FAT_DIRENTS_PER_SECTOR;
    *slotp = index % FAT_DIRENTS_PER_SECTOR;
    return 0;
}

static int
fat_file_update(struct inode *ip)
{
    struct fat_mount *fmp;
    struct buf *bp;
    unsigned char *data;
    unsigned char *entry;
    unsigned sector;
    unsigned slot;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0)
        return EIO;
    if (fmp->fm_read_only)
        return EROFS;
    if (ip->i_size < 0 ||
        (unsigned long long)ip->i_size > 0xffffffffULL)
        return EFBIG;
    error = fat_file_location(ip->i_number, &sector, &slot);
    if (error)
        return error;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    entry = data + slot * FAT_DIRENT_SIZE;
    if (!fat_dirent_is_visible(entry) ||
        (entry[11] & FAT_ATTR_DIRECTORY) != 0) {
        brelse(bp);
        return ENOENT;
    }
    fat_dirent_set_cluster_size(entry, (unsigned)ip->i_addr[0],
        (unsigned)ip->i_size);
    return fat_buffer_write(fmp, bp);
}

static int
fat_file_cluster_at(struct inode *ip, unsigned index, int allocate,
    unsigned *clusterp)
{
    struct fat_mount *fmp;
    unsigned cluster;
    unsigned next;
    unsigned new_cluster;
    unsigned i;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0 || clusterp == 0)
        return EIO;
    cluster = (unsigned)ip->i_addr[0];
    if (!fat_cluster_valid(&fmp->fm_volume, cluster)) {
        if (!allocate || cluster != 0)
            return EIO;
        error = fat_cluster_alloc(fmp, 0, &cluster);
        if (error)
            return error;
        ip->i_addr[0] = (daddr_t)cluster;
        error = fat_file_update(ip);
        if (error) {
            ip->i_addr[0] = 0;
            (void)fat_cluster_release(fmp, cluster);
            return error;
        }
    }
    for (i = 0; i < index; ++i) {
        error = fat_next_cluster(fmp, cluster, &next);
        if (error)
            return error;
        if (next == 0) {
            if (!allocate)
                return EIO;
            error = fat_cluster_alloc(fmp, 0, &new_cluster);
            if (error)
                return error;
            error = fat_cluster_write(fmp, cluster, new_cluster);
            if (error) {
                (void)fat_cluster_release(fmp, new_cluster);
                return error;
            }
            next = new_cluster;
        }
        cluster = next;
    }
    *clusterp = cluster;
    return 0;
}

static int
fat_chain_free(struct fat_mount *fmp, unsigned cluster)
{
    unsigned next;
    unsigned visited;
    int error;

    visited = 0;
    while (fat_cluster_valid(&fmp->fm_volume, cluster)) {
        if (++visited > fmp->fm_volume.fv_cluster_count)
            return EIO;
        error = fat_next_cluster(fmp, cluster, &next);
        if (error)
            return error;
        error = fat_cluster_release(fmp, cluster);
        if (error)
            return error;
        cluster = next;
    }
    return cluster == 0 ? 0 : EIO;
}

static int
fat_entry_insert_sector(struct fat_mount *fmp, unsigned sector,
    const unsigned char *short_name, unsigned attr, unsigned cluster,
    unsigned *slotp)
{
    struct buf *bp;
    unsigned char *data;
    unsigned char *entry;
    unsigned slot;
    int was_end;
    int error;

    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    for (slot = 0; slot < FAT_DIRENTS_PER_SECTOR; ++slot) {
        entry = data + slot * FAT_DIRENT_SIZE;
        if (entry[0] != FAT_DIRENT_END && entry[0] != FAT_DIRENT_DELETED)
            continue;
        if (entry[0] == FAT_DIRENT_END &&
            slot + 1u == FAT_DIRENTS_PER_SECTOR) {
            brelse(bp);
            return FAT_INSERT_END_LAST;
        }
        was_end = entry[0] == FAT_DIRENT_END;
        fat_dirent_encode(entry, short_name, attr, cluster, 0);
        if (was_end && slot + 1u < FAT_DIRENTS_PER_SECTOR)
            fat_zero(entry + FAT_DIRENT_SIZE, FAT_DIRENT_SIZE);
        error = fat_buffer_write(fmp, bp);
        if (error)
            return error;
        *slotp = slot;
        return 0;
    }
    brelse(bp);
    return ENOSPC;
}

static int
fat_entry_clear_first(struct fat_mount *fmp, unsigned sector)
{
    struct buf *bp;
    unsigned char *data;
    int error;

    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    fat_zero(data, FAT_DIRENT_SIZE);
    return fat_buffer_write(fmp, bp);
}

static int
fat_entry_insert_last(struct fat_mount *fmp, unsigned sector,
    const unsigned char *short_name, unsigned attr, unsigned cluster,
    unsigned *slotp)
{
    struct buf *bp;
    unsigned char *data;
    unsigned char *entry;
    int error;

    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    entry = data + (FAT_DIRENTS_PER_SECTOR - 1u) * FAT_DIRENT_SIZE;
    if (entry[0] != FAT_DIRENT_END) {
        brelse(bp);
        return EIO;
    }
    fat_dirent_encode(entry, short_name, attr, cluster, 0);
    error = fat_buffer_write(fmp, bp);
    if (error)
        return error;
    *slotp = FAT_DIRENTS_PER_SECTOR - 1u;
    return 0;
}

static int
fat_entry_insert(struct fat_mount *fmp, ino_t dir_ino,
    const unsigned char *short_name, unsigned attr, unsigned start_cluster,
    unsigned *sectorp, unsigned *slotp)
{
    unsigned cluster;
    unsigned next;
    unsigned new_cluster;
    unsigned sector;
    unsigned visited;
    unsigned i;
    int error;

    if (dir_ino == ROOTINO && fmp->fm_volume.fv_type == FAT_TYPE_16) {
        for (i = 0; i < fmp->fm_volume.fv_root_dir_sectors; ++i) {
            sector = fmp->fm_volume.fv_root_dir_start + i;
            error = fat_entry_insert_sector(fmp, sector, short_name, attr,
                start_cluster, slotp);
            if (error == FAT_INSERT_END_LAST) {
                if (i + 1u < fmp->fm_volume.fv_root_dir_sectors) {
                    error = fat_entry_clear_first(fmp, sector + 1u);
                    if (error)
                        return error;
                }
                error = fat_entry_insert_last(fmp, sector, short_name, attr,
                    start_cluster, slotp);
            }
            if (error == 0) {
                *sectorp = sector;
                return 0;
            }
            if (error != ENOSPC)
                return error;
        }
        return ENOSPC;
    }

    cluster = dir_ino == ROOTINO ? fmp->fm_volume.fv_root_cluster :
        ((unsigned)dir_ino & FAT_DIR_CLUSTER_MASK);
    if (!fat_cluster_valid(&fmp->fm_volume, cluster))
        return EIO;
    visited = 0;
    for (;;) {
        if (++visited > fmp->fm_volume.fv_cluster_count)
            return EIO;
        sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
        for (i = 0; i < fmp->fm_volume.fv_sectors_per_cluster; ++i) {
            error = fat_entry_insert_sector(fmp, sector + i, short_name,
                attr, start_cluster, slotp);
            if (error == FAT_INSERT_END_LAST) {
                if (i + 1u < fmp->fm_volume.fv_sectors_per_cluster) {
                    error = fat_entry_clear_first(fmp, sector + i + 1u);
                    if (error)
                        return error;
                } else {
                    error = fat_next_cluster(fmp, cluster, &next);
                    if (error)
                        return error;
                    if (next != 0) {
                        error = fat_entry_clear_first(fmp,
                            fat_cluster_first_sector(&fmp->fm_volume,
                            next));
                        if (error)
                            return error;
                    }
                }
                error = fat_entry_insert_last(fmp, sector + i,
                    short_name, attr, start_cluster, slotp);
            }
            if (error == 0) {
                *sectorp = sector + i;
                return 0;
            }
            if (error != ENOSPC)
                return error;
        }
        error = fat_next_cluster(fmp, cluster, &next);
        if (error)
            return error;
        if (next != 0) {
            cluster = next;
            continue;
        }
        error = fat_cluster_alloc(fmp, 1, &new_cluster);
        if (error)
            return error;
        error = fat_cluster_write(fmp, cluster, new_cluster);
        if (error) {
            (void)fat_cluster_release(fmp, new_cluster);
            return error;
        }
        cluster = new_cluster;
    }
}

static int
fat_entry_delete_at(struct fat_mount *fmp, unsigned sector, unsigned slot,
    int directory)
{
    struct buf *bp;
    unsigned char *data;
    unsigned char *entry;
    int error;

    if (slot >= FAT_DIRENTS_PER_SECTOR)
        return EINVAL;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    entry = data + slot * FAT_DIRENT_SIZE;
    if (!fat_dirent_is_visible(entry) ||
        ((entry[11] & FAT_ATTR_DIRECTORY) != 0) != directory) {
        brelse(bp);
        return ENOENT;
    }
    entry[0] = FAT_DIRENT_DELETED;
    return fat_buffer_write(fmp, bp);
}

static int
fat_entry_delete(struct fat_mount *fmp, ino_t ino)
{
    unsigned sector;
    unsigned slot;
    int error;

    error = fat_file_location(ino, &sector, &slot);
    if (error)
        return error;
    return fat_entry_delete_at(fmp, sector, slot, 0);
}

static int
fat_entry_rename_at(struct fat_mount *fmp, unsigned sector, unsigned slot,
    const unsigned char *old_name, const unsigned char *new_name,
    int directory, unsigned cluster)
{
    struct buf *bp;
    struct fat_dirent dirent;
    unsigned char *data;
    unsigned char *entry;
    int error;

    if (slot >= FAT_DIRENTS_PER_SECTOR)
        return EINVAL;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    entry = data + slot * FAT_DIRENT_SIZE;
    if (!fat_dirent_is_visible(entry) ||
        ((entry[11] & FAT_ATTR_DIRECTORY) != 0) != directory ||
        bcmp(entry, old_name, 11u) != 0) {
        brelse(bp);
        return EOPNOTSUPP;
    }
    fat_dirent_parse(&dirent, entry);
    if (fat_dirent_cluster(fmp, &dirent) != cluster) {
        brelse(bp);
        return EIO;
    }
    bcopy(new_name, entry, 11u);
    return fat_buffer_write(fmp, bp);
}

static int
fat_directory_initialize(struct fat_mount *fmp, unsigned cluster,
    unsigned parent_cluster)
{
    struct buf *bp;
    unsigned char *data;
    unsigned sector;
    int error;

    sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
    if (sector == 0xffffffffu)
        return EIO;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    fat_directory_encode(data, cluster, parent_cluster);
    return fat_buffer_write(fmp, bp);
}

static int
fat_directory_validate(struct fat_mount *fmp, unsigned cluster,
    unsigned parent_cluster, int parent_is_root)
{
    struct buf *bp;
    struct fat_dirent dot;
    struct fat_dirent dotdot;
    unsigned char *data;
    unsigned sector;
    unsigned actual_parent;
    int error;

    sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
    if (sector == 0xffffffffu)
        return EIO;
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    fat_dirent_parse(&dot, data);
    fat_dirent_parse(&dotdot, data + FAT_DIRENT_SIZE);
    actual_parent = fat_dirent_cluster(fmp, &dotdot);
    if (bcmp(data, fat_dot_name, 11u) != 0 ||
        bcmp(data + FAT_DIRENT_SIZE, fat_dotdot_name, 11u) != 0 ||
        (dot.fd_attr & FAT_ATTR_DIRECTORY) == 0 ||
        (dotdot.fd_attr & FAT_ATTR_DIRECTORY) == 0 ||
        fat_dirent_cluster(fmp, &dot) != cluster ||
        (!parent_is_root && actual_parent != parent_cluster) ||
        (parent_is_root && actual_parent != 0 &&
        actual_parent != parent_cluster)) {
        brelse(bp);
        return EIO;
    }
    brelse(bp);
    return 0;
}

static int
fat_directory_nonempty_entry(struct fat_mount *fmp,
    const unsigned char *entry, unsigned sector, unsigned slot,
    const char *name, unsigned namelen, void *arg)
{
    int *nonemptyp;

    (void)fmp;
    (void)entry;
    (void)sector;
    (void)slot;
    if ((namelen == 1u && name[0] == '.') ||
        (namelen == 2u && name[0] == '.' && name[1] == '.'))
        return 0;
    nonemptyp = (int *)arg;
    *nonemptyp = 1;
    return FAT_SCAN_FOUND;
}

static int
fat_directory_is_empty(struct fat_mount *fmp, ino_t ino, unsigned cluster,
    unsigned parent_cluster, int parent_is_root)
{
    int nonempty;
    int error;

    error = fat_directory_validate(fmp, cluster, parent_cluster,
        parent_is_root);
    if (error)
        return error;
    nonempty = 0;
    error = fat_scan_directory(fmp, ino, fat_directory_nonempty_entry,
        &nonempty);
    if (error == FAT_SCAN_FOUND && nonempty)
        return ENOTEMPTY;
    return error;
}

static int
fat_directory_find_entry(struct fat_mount *fmp,
    const unsigned char *entry, unsigned sector, unsigned slot,
    const char *name, unsigned namelen, void *arg)
{
    struct fat_entry_find *find;
    struct fat_dirent dirent;

    (void)name;
    (void)namelen;
    find = (struct fat_entry_find *)arg;
    if ((entry[11] & FAT_ATTR_DIRECTORY) == 0 ||
        bcmp(entry, find->ff_name, 11u) != 0)
        return 0;
    fat_dirent_parse(&dirent, entry);
    if (fat_dirent_cluster(fmp, &dirent) != find->ff_cluster)
        return 0;
    find->ff_sector = sector;
    find->ff_slot = slot;
    find->ff_found = 1;
    return FAT_SCAN_FOUND;
}

static int
fat_directory_entry_location(struct fat_mount *fmp, ino_t parent_ino,
    const unsigned char *short_name, unsigned cluster, unsigned *sectorp,
    unsigned *slotp)
{
    struct fat_entry_find find;
    int error;

    fat_zero(&find, sizeof(find));
    find.ff_name = short_name;
    find.ff_cluster = cluster;
    error = fat_scan_directory(fmp, parent_ino, fat_directory_find_entry,
        &find);
    if (error != 0 && error != FAT_SCAN_FOUND)
        return error;
    if (!find.ff_found)
        return ENOENT;
    *sectorp = find.ff_sector;
    *slotp = find.ff_slot;
    return 0;
}

static int
fat_parent_ino(struct fat_mount *fmp, ino_t ino, ino_t *parentp)
{
    struct buf *bp;
    struct fat_dirent dirent;
    unsigned char *data;
    unsigned cluster;
    unsigned sector;
    unsigned parent;
    int error;

    if (ino == ROOTINO) {
        *parentp = ROOTINO;
        return 0;
    }
    cluster = (unsigned)ino & FAT_DIR_CLUSTER_MASK;
    if (!fat_cluster_valid(&fmp->fm_volume, cluster))
        return EIO;
    sector = fat_cluster_first_sector(&fmp->fm_volume, cluster);
    error = fat_sector_get(fmp, sector, &bp, &data);
    if (error)
        return error;
    fat_dirent_parse(&dirent, data + FAT_DIRENT_SIZE);
    if (data[FAT_DIRENT_SIZE] != '.' ||
        data[FAT_DIRENT_SIZE + 1u] != '.' ||
        data[FAT_DIRENT_SIZE + 2u] != ' ' ||
        (dirent.fd_attr & FAT_ATTR_DIRECTORY) == 0) {
        brelse(bp);
        return EIO;
    }
    brelse(bp);
    parent = fat_dirent_cluster(fmp, &dirent);
    if (parent == 0 || parent == fmp->fm_volume.fv_root_cluster) {
        *parentp = ROOTINO;
        return 0;
    }
    if (!fat_cluster_valid(&fmp->fm_volume, parent))
        return EIO;
    *parentp = fat_directory_ino(parent);
    return 0;
}

static int
fat_pad_dirblock(struct fat_dir_build *build, unsigned count)
{
    struct direct *last;

    if (count == 0)
        return 0;
    if (build->fb_last_offset < 0)
        return EIO;
    if (build->fb_block != 0 &&
        build->fb_last_offset >= build->fb_block_base &&
        build->fb_last_offset < build->fb_block_base + DIRBLKSIZ) {
        last = (struct direct *)(build->fb_block +
            ((unsigned)build->fb_last_offset & (DIRBLKSIZ - 1u)));
        last->d_reclen += count;
    }
    build->fb_offset += count;
    return 0;
}

static int
fat_emit_dirent(struct fat_dir_build *build, ino_t ino, const char *name,
    unsigned namelen)
{
    struct direct d;
    struct direct *dp;
    unsigned reclen;
    unsigned inblock;
    unsigned remaining;
    int error;

    fat_zero(&d, sizeof(d));
    d.d_namlen = namelen;
    reclen = DIRSIZ(&d);
    if ((unsigned long)build->fb_offset > 0x7ffffffful - reclen)
        return EFBIG;
    inblock = (unsigned)build->fb_offset & (DIRBLKSIZ - 1u);
    if (inblock + reclen > DIRBLKSIZ) {
        remaining = DIRBLKSIZ - inblock;
        error = fat_pad_dirblock(build, remaining);
        if (error)
            return error;
        inblock = 0;
    }
    if (build->fb_block != 0 &&
        build->fb_offset >= build->fb_block_base &&
        build->fb_offset < build->fb_block_base + DIRBLKSIZ) {
        dp = (struct direct *)(build->fb_block + inblock);
        fat_zero(dp, reclen);
        dp->d_ino = ino;
        dp->d_reclen = reclen;
        dp->d_namlen = namelen;
        bcopy(name, dp->d_name, namelen);
        dp->d_name[namelen] = '\0';
    }
    build->fb_last_offset = build->fb_offset;
    build->fb_offset += reclen;
    return 0;
}

static int
fat_dir_build_entry(struct fat_mount *fmp, const unsigned char *entry,
    unsigned sector, unsigned slot, const char *name, unsigned namelen,
    void *arg)
{
    struct fat_dir_build *build;
    struct fat_dirent dirent;
    ino_t ino;
    int error;

    if ((namelen == 1 && name[0] == '.') ||
        (namelen == 2 && name[0] == '.' && name[1] == '.'))
        return 0;
    build = (struct fat_dir_build *)arg;
    fat_dirent_parse(&dirent, entry);
    dirent.fd_cluster = fat_dirent_cluster(fmp, &dirent);
    if ((dirent.fd_attr & FAT_ATTR_DIRECTORY) != 0) {
        if (!fat_cluster_valid(&fmp->fm_volume, dirent.fd_cluster))
            return EIO;
        ino = fat_directory_ino(dirent.fd_cluster);
    } else {
        error = fat_file_ino(sector, slot, &ino);
        if (error)
            return error;
    }
    return fat_emit_dirent(build, ino, name, namelen);
}

static int
fat_dir_build(struct fat_mount *fmp, ino_t ino, char *block,
    off_t block_base, off_t *sizep)
{
    struct fat_dir_build build;
    ino_t parent;
    unsigned inblock;
    unsigned remaining;
    int error;

    build.fb_block = block;
    build.fb_block_base = block_base;
    build.fb_offset = 0;
    build.fb_last_offset = -1;
    if (block != 0)
        fat_zero(block, DIRBLKSIZ);
    error = fat_parent_ino(fmp, ino, &parent);
    if (error)
        return error;
    error = fat_emit_dirent(&build, ino, ".", 1);
    if (error)
        return error;
    error = fat_emit_dirent(&build, parent, "..", 2);
    if (error)
        return error;
    error = fat_scan_directory(fmp, ino, fat_dir_build_entry, &build);
    if (error)
        return error;

    inblock = (unsigned)build.fb_offset & (DIRBLKSIZ - 1u);
    if (inblock != 0) {
        remaining = DIRBLKSIZ - inblock;
        error = fat_pad_dirblock(&build, remaining);
        if (error)
            return error;
    }
    *sizep = build.fb_offset;
    return 0;
}

static int
fat_load_inode(struct mount *mp, struct inode *ip)
{
    struct fat_mount *fmp;
    struct fat_dirent dirent;
    unsigned char entry[FAT_DIRENT_SIZE];
    unsigned index;
    unsigned sector;
    unsigned slot;
    unsigned cluster;
    off_t dir_size;
    int error;

    fmp = (struct fat_mount *)mp->m_data;
    fat_zero(&ip->i_ic1, sizeof(ip->i_ic1));
    fat_zero(&ip->i_ic2, sizeof(ip->i_ic2));
    fat_zero(ip->i_addr, sizeof(ip->i_addr));
    ip->i_flags = 0;
    ip->i_uid = 0;
    ip->i_gid = 0;
    ip->i_atime = 0;
    ip->i_mtime = 0;
    ip->i_ctime = 0;

    if (ip->i_number == ROOTINO || ip->i_number >= FAT_DIR_INO_BASE) {
        if (ip->i_number == ROOTINO)
            cluster = fmp->fm_volume.fv_root_cluster;
        else
            cluster = (unsigned)ip->i_number & FAT_DIR_CLUSTER_MASK;
        if (fmp->fm_volume.fv_type == FAT_TYPE_32 ||
            ip->i_number != ROOTINO) {
            if (!fat_cluster_valid(&fmp->fm_volume, cluster))
                return ENOENT;
        }
        error = fat_dir_build(fmp, ip->i_number, 0, 0, &dir_size);
        if (error)
            return error;
        ip->i_mode = IFDIR | (fmp->fm_read_only ? 0555 : 0777);
        ip->i_nlink = 2;
        ip->i_size = dir_size;
        ip->i_addr[0] = cluster;
        return 0;
    }
    if (ip->i_number < FAT_FILE_INO_BASE)
        return ENOENT;
    index = (unsigned)ip->i_number - FAT_FILE_INO_BASE;
    sector = index / FAT_DIRENTS_PER_SECTOR;
    slot = index % FAT_DIRENTS_PER_SECTOR;
    error = fat_entry_read(fmp, sector, slot, entry);
    if (error)
        return error;
    if (!fat_dirent_is_visible(entry) ||
        (entry[11] & FAT_ATTR_DIRECTORY) != 0)
        return ENOENT;
    fat_dirent_parse(&dirent, entry);
    dirent.fd_cluster = fat_dirent_cluster(fmp, &dirent);
    if (dirent.fd_size != 0 &&
        !fat_cluster_valid(&fmp->fm_volume, dirent.fd_cluster))
        return EIO;
    ip->i_mode = IFREG | ((fmp->fm_read_only ||
        (dirent.fd_attr & FAT_ATTR_READ_ONLY) != 0) ? 0444 : 0666);
    ip->i_nlink = 1;
    ip->i_size = (off_t)dirent.fd_size;
    ip->i_addr[0] = (daddr_t)dirent.fd_cluster;
    ip->i_atime = fat_timestamp(dirent.fd_access_date, 0);
    ip->i_mtime = fat_timestamp(dirent.fd_modify_date,
        dirent.fd_modify_time);
    ip->i_ctime = fat_timestamp(dirent.fd_create_date,
        dirent.fd_create_time);
    return 0;
}

static struct buf *
fat_blkatoff(struct inode *ip, off_t offset, char **res)
{
    struct fat_mount *fmp;
    struct buf *bp;
    off_t size;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0) {
        u.u_error = EIO;
        return 0;
    }
    bp = geteblk();
    fat_zero(bp->b_addr, MAXBSIZE);
    error = fat_dir_build(fmp, ip->i_number, bp->b_addr,
        offset & ~(DIRBLKSIZ - 1), &size);
    if (error) {
        brelse(bp);
        u.u_error = error;
        return 0;
    }
    bp->b_resid = 0;
    bp->b_flags |= B_DONE;
    if (res != 0)
        *res = bp->b_addr + (offset & (DIRBLKSIZ - 1));
    return bp;
}

static int
fat_read_directory(struct inode *ip, struct uio *uio)
{
    struct buf *bp;
    unsigned offset;
    unsigned n;
    int error;

    while (uio->uio_resid != 0 && uio->uio_offset < ip->i_size) {
        bp = fat_blkatoff(ip, uio->uio_offset, 0);
        if (bp == 0)
            return u.u_error ? u.u_error : EIO;
        offset = (unsigned)uio->uio_offset & (DIRBLKSIZ - 1u);
        n = MIN((u_int)(DIRBLKSIZ - offset), uio->uio_resid);
        if (uio->uio_offset + n > ip->i_size)
            n = ip->i_size - uio->uio_offset;
        error = uiomove(bp->b_addr + offset, n, uio);
        brelse(bp);
        if (error)
            return error;
    }
    return 0;
}

static int
fat_read_file(struct inode *ip, struct uio *uio)
{
    struct fat_mount *fmp;
    struct buf *bp;
    unsigned char *data;
    unsigned cluster;
    unsigned cluster_bytes;
    unsigned within;
    unsigned skip;
    unsigned visited;
    unsigned sector;
    unsigned offset;
    unsigned n;
    unsigned next;
    int error;

    if (uio->uio_offset < 0)
        return EINVAL;
    if (uio->uio_offset >= ip->i_size || uio->uio_resid == 0)
        return 0;
    fmp = fat_mount_from_inode(ip);
    if (fmp == 0)
        return EIO;
    cluster = (unsigned)ip->i_addr[0];
    if (!fat_cluster_valid(&fmp->fm_volume, cluster))
        return EIO;
    cluster_bytes = fmp->fm_volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    skip = (unsigned)uio->uio_offset / cluster_bytes;
    within = (unsigned)uio->uio_offset % cluster_bytes;
    visited = 0;
    while (skip-- != 0) {
        if (++visited > fmp->fm_volume.fv_cluster_count)
            return EIO;
        error = fat_next_cluster(fmp, cluster, &next);
        if (error || next == 0)
            return EIO;
        cluster = next;
    }

    while (uio->uio_resid != 0 && uio->uio_offset < ip->i_size) {
        sector = fat_cluster_first_sector(&fmp->fm_volume, cluster) +
            within / FAT_SECTOR_SIZE;
        offset = within & (FAT_SECTOR_SIZE - 1u);
        n = MIN((u_int)(FAT_SECTOR_SIZE - offset), uio->uio_resid);
        if (uio->uio_offset + n > ip->i_size)
            n = ip->i_size - uio->uio_offset;
        error = fat_sector_get(fmp, sector, &bp, &data);
        if (error)
            return error;
        error = uiomove((caddr_t)data + offset, n, uio);
        brelse(bp);
        if (error)
            return error;
        within += n;
        if (within == cluster_bytes && uio->uio_offset < ip->i_size) {
            if (++visited > fmp->fm_volume.fv_cluster_count)
                return EIO;
            error = fat_next_cluster(fmp, cluster, &next);
            if (error || next == 0)
                return EIO;
            cluster = next;
            within = 0;
        }
    }
    return 0;
}

static int fat_zero_file_range(struct inode *, off_t, off_t);

static int
fat_write_file(struct inode *ip, struct uio *uio, int ioflag)
{
    struct fat_mount *fmp;
    struct buf *bp;
    unsigned char *data;
    unsigned cluster;
    unsigned cluster_bytes;
    unsigned cluster_index;
    unsigned new_cluster;
    unsigned next;
    unsigned within;
    unsigned sector;
    unsigned offset;
    unsigned n;
    off_t new_size;
    int error;
    int update_error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0)
        return EIO;
    if (fmp->fm_read_only)
        return EROFS;
    if ((ip->i_mode & 0222) == 0)
        return EACCES;
    if ((ioflag & IO_APPEND) != 0)
        uio->uio_offset = ip->i_size;
    if (uio->uio_offset < 0 ||
        (unsigned long long)uio->uio_offset > 0xffffffffULL ||
        (unsigned long long)uio->uio_resid >
        0xffffffffULL - (unsigned long long)uio->uio_offset)
        return EFBIG;

    if (uio->uio_resid != 0 && uio->uio_offset > ip->i_size) {
        error = fat_zero_file_range(ip, ip->i_size, uio->uio_offset);
        if (error)
            return error;
    }

    cluster_bytes = fmp->fm_volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    new_size = ip->i_size;
    error = 0;
    if (uio->uio_resid == 0)
        return 0;
    cluster_index = (unsigned)uio->uio_offset / cluster_bytes;
    within = (unsigned)uio->uio_offset % cluster_bytes;
    error = fat_file_cluster_at(ip, cluster_index, 1, &cluster);
    if (error)
        return error;
    while (uio->uio_resid != 0) {
        sector = fat_cluster_first_sector(&fmp->fm_volume, cluster) +
            within / FAT_SECTOR_SIZE;
        offset = within & (FAT_SECTOR_SIZE - 1u);
        if (offset == 0 && (sector & 1u) == 0 &&
            uio->uio_resid >= DEV_BSIZE &&
            cluster_bytes - within >= DEV_BSIZE) {
            /* The whole native buffer is replaced; do not read it first. */
            bp = getblk(fmp->fm_dev, (daddr_t)(sector >> 1));
            data = (unsigned char *)bp->b_addr;
            bp->b_resid = 0;
            n = DEV_BSIZE;
        } else {
            n = MIN((u_int)(FAT_SECTOR_SIZE - offset), uio->uio_resid);
            error = fat_sector_get(fmp, sector, &bp, &data);
            if (error)
                break;
        }
        error = uiomove((caddr_t)data + offset, n, uio);
        if (error) {
            brelse(bp);
            break;
        }
        error = fat_buffer_write(fmp, bp);
        if (error)
            break;
        if (uio->uio_offset > new_size)
            new_size = uio->uio_offset;
        within += n;
        if (within == cluster_bytes && uio->uio_resid != 0) {
            error = fat_next_cluster(fmp, cluster, &next);
            if (error)
                break;
            if (next == 0) {
                error = fat_cluster_alloc(fmp, 0, &new_cluster);
                if (error)
                    break;
                error = fat_cluster_write(fmp, cluster, new_cluster);
                if (error) {
                    (void)fat_cluster_release(fmp, new_cluster);
                    break;
                }
                next = new_cluster;
            }
            cluster = next;
            within = 0;
        }
    }
    if (new_size != ip->i_size) {
        ip->i_size = new_size;
        update_error = fat_file_update(ip);
        if (error == 0)
            error = update_error;
    }
    return error;
}

static int
fat_zero_file_range(struct inode *ip, off_t start, off_t end)
{
    struct fat_mount *fmp;
    struct buf *bp;
    unsigned char *data;
    unsigned cluster;
    unsigned cluster_bytes;
    unsigned cluster_index;
    unsigned within;
    unsigned sector;
    unsigned offset;
    unsigned n;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0)
        return EIO;
    cluster_bytes = fmp->fm_volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    while (start < end) {
        cluster_index = (unsigned)start / cluster_bytes;
        within = (unsigned)start % cluster_bytes;
        error = fat_file_cluster_at(ip, cluster_index, 1, &cluster);
        if (error)
            return error;
        sector = fat_cluster_first_sector(&fmp->fm_volume, cluster) +
            within / FAT_SECTOR_SIZE;
        offset = within & (FAT_SECTOR_SIZE - 1u);
        n = FAT_SECTOR_SIZE - offset;
        if ((off_t)n > end - start)
            n = (unsigned)(end - start);
        error = fat_sector_get(fmp, sector, &bp, &data);
        if (error)
            return error;
        fat_zero(data + offset, n);
        error = fat_buffer_write(fmp, bp);
        if (error)
            return error;
        start += n;
    }
    return 0;
}

static int
fat_truncate(struct inode *ip, off_t length, int ioflags)
{
    struct fat_mount *fmp;
    unsigned old_cluster;
    unsigned keep_cluster;
    unsigned tail;
    unsigned cluster_bytes;
    unsigned keep_index;
    int error;

    (void)ioflags;
    fmp = fat_mount_from_inode(ip);
    if (fmp == 0)
        return EIO;
    if (fmp->fm_read_only)
        return EROFS;
    if ((ip->i_mode & IFMT) != IFREG)
        return EISDIR;
    if (length < 0 || (unsigned long long)length > 0xffffffffULL)
        return EFBIG;
    if ((off_t)length == ip->i_size)
        return 0;
    if ((off_t)length > ip->i_size) {
        error = fat_zero_file_range(ip, ip->i_size, (off_t)length);
        if (error)
            return error;
        ip->i_size = (off_t)length;
        return fat_file_update(ip);
    }

    old_cluster = (unsigned)ip->i_addr[0];
    if (length == 0) {
        ip->i_addr[0] = 0;
        ip->i_size = 0;
        error = fat_file_update(ip);
        if (error) {
            ip->i_addr[0] = (daddr_t)old_cluster;
            return error;
        }
        if (fat_cluster_valid(&fmp->fm_volume, old_cluster))
            return fat_chain_free(fmp, old_cluster);
        return old_cluster == 0 ? 0 : EIO;
    }

    cluster_bytes = fmp->fm_volume.fv_sectors_per_cluster *
        FAT_SECTOR_SIZE;
    keep_index = ((unsigned)length - 1u) / cluster_bytes;
    error = fat_file_cluster_at(ip, keep_index, 0, &keep_cluster);
    if (error)
        return error;
    error = fat_next_cluster(fmp, keep_cluster, &tail);
    if (error)
        return error;
    ip->i_size = (off_t)length;
    error = fat_file_update(ip);
    if (error)
        return error;
    if (tail == 0)
        return 0;
    error = fat_cluster_write(fmp, keep_cluster,
        fat_eoc_value(&fmp->fm_volume));
    if (error)
        return error;
    return fat_chain_free(fmp, tail);
}

static int
fat_rwip(struct inode *ip, struct uio *uio, int ioflag)
{
    int type;

    (void)ioflag;
    type = ip->i_mode & IFMT;
    if (type == IFDIR) {
        if (uio->uio_rw != UIO_READ)
            return EISDIR;
        return fat_read_directory(ip, uio);
    }
    if (type == IFREG) {
        if (uio->uio_rw == UIO_READ)
            return fat_read_file(ip, uio);
        return fat_write_file(ip, uio, ioflag);
    }
    return EFTYPE;
}

static int
fat_create(struct inode *pdir, struct nameidata *ndp, int mode,
    struct inode **ipp)
{
    struct fat_mount *fmp;
    struct inode *ip;
    struct buf *bp;
    unsigned char *data;
    unsigned char short_name[11];
    unsigned sector;
    unsigned slot;
    ino_t ino;
    off_t dir_size;
    int error;

    fmp = fat_mount_from_inode(pdir);
    if (fmp == 0)
        return EIO;
    if (fmp->fm_read_only)
        return EROFS;
    if ((mode & IFMT) != 0 && (mode & IFMT) != IFREG)
        return EOPNOTSUPP;
    error = fat_short_name_encode(ndp->ni_dent.d_name,
        ndp->ni_dent.d_namlen, short_name);
    if (error != FAT_PARSE_OK)
        return error == FAT_PARSE_UNSUPPORTED ? EOPNOTSUPP : EINVAL;
    error = fat_entry_insert(fmp, pdir->i_number, short_name,
        FAT_ATTR_ARCHIVE, 0, &sector, &slot);
    if (error)
        return error;
    error = fat_file_ino(sector, slot, &ino);
    if (error) {
        if (fat_sector_get(fmp, sector, &bp, &data) == 0) {
            data[slot * FAT_DIRENT_SIZE] = FAT_DIRENT_DELETED;
            (void)fat_buffer_write(fmp, bp);
        }
        return error;
    }
    ip = iget(pdir->i_dev, pdir->i_fs, ino);
    if (ip == 0) {
        (void)fat_entry_delete(fmp, ino);
        return u.u_error ? u.u_error : EIO;
    }
    if (fat_dir_build(fmp, pdir->i_number, 0, 0, &dir_size) == 0)
        pdir->i_size = dir_size;
    cacheinval(pdir);
    *ipp = ip;
    return 0;
}

static int
fat_mkdir(struct inode *pdir, struct nameidata *ndp, int mode)
{
    struct fat_mount *fmp;
    unsigned char short_name[11];
    unsigned cluster;
    unsigned parent_cluster;
    unsigned dotdot_cluster;
    unsigned sector;
    unsigned slot;
    off_t dir_size;
    int error;

    fmp = fat_mount_from_inode(pdir);
    if (fmp == 0)
        return EIO;
    if (fmp->fm_read_only)
        return EROFS;
    if ((mode & IFMT) != IFDIR)
        return EINVAL;
    error = fat_short_name_encode(ndp->ni_dent.d_name,
        ndp->ni_dent.d_namlen, short_name);
    if (error != FAT_PARSE_OK)
        return error == FAT_PARSE_UNSUPPORTED ? EOPNOTSUPP : EINVAL;
    parent_cluster = (unsigned)pdir->i_addr[0];
    if (pdir->i_number != ROOTINO &&
        !fat_cluster_valid(&fmp->fm_volume, parent_cluster))
        return EIO;
    dotdot_cluster = pdir->i_number == ROOTINO ? 0 : parent_cluster;

    error = fat_cluster_alloc(fmp, 1, &cluster);
    if (error)
        return error;
    error = fat_directory_initialize(fmp, cluster, dotdot_cluster);
    if (error) {
        (void)fat_chain_free(fmp, cluster);
        return error;
    }
    error = fat_entry_insert(fmp, pdir->i_number, short_name,
        FAT_ATTR_DIRECTORY, cluster, &sector, &slot);
    if (error) {
        (void)fat_chain_free(fmp, cluster);
        return error;
    }
    if (fat_dir_build(fmp, pdir->i_number, 0, 0, &dir_size) == 0)
        pdir->i_size = dir_size;
    cacheinval(pdir);
    return 0;
}

static int
fat_remove(struct inode *pdir, struct inode *ip, struct nameidata *ndp)
{
    struct fat_mount *fmp;
    unsigned char entry[FAT_DIRENT_SIZE];
    unsigned char short_name[11];
    unsigned cluster;
    unsigned sector;
    unsigned slot;
    off_t dir_size;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0 || fmp != fat_mount_from_inode(pdir))
        return EXDEV;
    if (fmp->fm_read_only)
        return EROFS;
    if ((ip->i_mode & IFMT) != IFREG)
        return EISDIR;
    if (ip->i_count > 1u)
        return EBUSY;
    error = fat_short_name_encode(ndp->ni_dent.d_name,
        ndp->ni_dent.d_namlen, short_name);
    if (error != FAT_PARSE_OK)
        return EOPNOTSUPP;
    error = fat_file_location(ip->i_number, &sector, &slot);
    if (error)
        return error;
    error = fat_entry_read(fmp, sector, slot, entry);
    if (error)
        return error;
    if (bcmp(entry, short_name, sizeof(short_name)) != 0)
        return EOPNOTSUPP;
    cluster = (unsigned)ip->i_addr[0];
    error = fat_entry_delete(fmp, ip->i_number);
    if (error)
        return error;
    ip->i_addr[0] = 0;
    ip->i_size = 0;
    ip->i_nlink = 0;
    cacheinval(ip);
    if (fat_dir_build(fmp, pdir->i_number, 0, 0, &dir_size) == 0)
        pdir->i_size = dir_size;
    cacheinval(pdir);
    if (fat_cluster_valid(&fmp->fm_volume, cluster))
        return fat_chain_free(fmp, cluster);
    return cluster == 0 ? 0 : EIO;
}

static int
fat_rmdir(struct inode *pdir, struct inode *ip, struct nameidata *ndp)
{
    struct fat_mount *fmp;
    unsigned char short_name[11];
    unsigned cluster;
    unsigned parent_cluster;
    unsigned sector;
    unsigned slot;
    off_t dir_size;
    int error;

    fmp = fat_mount_from_inode(ip);
    if (fmp == 0 || fmp != fat_mount_from_inode(pdir))
        return EXDEV;
    if (fmp->fm_read_only)
        return EROFS;
    if ((ip->i_mode & IFMT) != IFDIR)
        return ENOTDIR;
    if (ip->i_number == ROOTINO || ip->i_count > 1u)
        return EBUSY;
    error = fat_short_name_encode(ndp->ni_dent.d_name,
        ndp->ni_dent.d_namlen, short_name);
    if (error != FAT_PARSE_OK)
        return EOPNOTSUPP;
    cluster = (unsigned)ip->i_addr[0];
    if (!fat_cluster_valid(&fmp->fm_volume, cluster))
        return EIO;
    parent_cluster = (unsigned)pdir->i_addr[0];
    if (pdir->i_number != ROOTINO &&
        !fat_cluster_valid(&fmp->fm_volume, parent_cluster))
        return EIO;
    error = fat_directory_is_empty(fmp, ip->i_number, cluster,
        parent_cluster, pdir->i_number == ROOTINO);
    if (error)
        return error;
    error = fat_directory_entry_location(fmp, pdir->i_number, short_name,
        cluster, &sector, &slot);
    if (error)
        return error;
    error = fat_entry_delete_at(fmp, sector, slot, 1);
    if (error)
        return error;

    ip->i_addr[0] = 0;
    ip->i_size = 0;
    ip->i_nlink = 0;
    cacheinval(ip);
    if (fat_dir_build(fmp, pdir->i_number, 0, 0, &dir_size) == 0)
        pdir->i_size = dir_size;
    cacheinval(pdir);
    return fat_chain_free(fmp, cluster);
}

static int
fat_rename(struct inode *from_pdir, struct inode *from_ip,
    struct nameidata *from_ndp, struct inode *to_pdir, struct inode *to_ip,
    struct nameidata *to_ndp)
{
    struct fat_mount *fmp;
    unsigned char from_name[11];
    unsigned char to_name[11];
    unsigned cluster;
    unsigned sector;
    unsigned slot;
    int directory;
    int error;

    fmp = fat_mount_from_inode(from_ip);
    if (fmp == 0 || fmp != fat_mount_from_inode(from_pdir) ||
        fmp != fat_mount_from_inode(to_pdir) ||
        (to_ip != 0 && fmp != fat_mount_from_inode(to_ip)))
        return EXDEV;
    if (fmp->fm_read_only)
        return EROFS;
    if (from_pdir->i_number != to_pdir->i_number)
        return EOPNOTSUPP;
    error = fat_short_name_encode(from_ndp->ni_dent.d_name,
        from_ndp->ni_dent.d_namlen, from_name);
    if (error != FAT_PARSE_OK)
        return EOPNOTSUPP;
    error = fat_short_name_encode(to_ndp->ni_dent.d_name,
        to_ndp->ni_dent.d_namlen, to_name);
    if (error != FAT_PARSE_OK)
        return EOPNOTSUPP;
    if (to_ip == from_ip)
        return 0;

    directory = (from_ip->i_mode & IFMT) == IFDIR;
    if (!directory && (from_ip->i_mode & IFMT) != IFREG)
        return EFTYPE;
    if (to_ip != 0) {
        if (directory && (to_ip->i_mode & IFMT) != IFDIR)
            return ENOTDIR;
        if (!directory && (to_ip->i_mode & IFMT) == IFDIR)
            return EISDIR;
    }
    cluster = (unsigned)from_ip->i_addr[0];
    if (directory) {
        if (!fat_cluster_valid(&fmp->fm_volume, cluster))
            return EIO;
        error = fat_directory_entry_location(fmp, from_pdir->i_number,
            from_name, cluster, &sector, &slot);
    } else {
        if (cluster != 0 && !fat_cluster_valid(&fmp->fm_volume, cluster))
            return EIO;
        error = fat_file_location(from_ip->i_number, &sector, &slot);
    }
    if (error)
        return error;

    if (to_ip != 0) {
        error = directory ? fat_rmdir(to_pdir, to_ip, to_ndp) :
            fat_remove(to_pdir, to_ip, to_ndp);
        if (error)
            return error;
    }
    error = fat_entry_rename_at(fmp, sector, slot, from_name, to_name,
        directory, cluster);
    if (error)
        return error;
    cacheinval(from_pdir);
    cacheinval(from_ip);
    return 0;
}

static int
fat_statfs(struct mount *mp, struct statfs *sbp)
{
    struct fat_mount *fmp;
    struct statfs sfs;

    fmp = (struct fat_mount *)mp->m_data;
    fat_zero(&sfs, sizeof(sfs));
    sfs.f_type = MOUNT_FAT;
    sfs.f_flags = mp->m_flags & MNT_VISFLAGMASK;
    sfs.f_bsize = FAT_SECTOR_SIZE;
    sfs.f_iosize = MAXBSIZE;
    sfs.f_blocks = fmp->fm_volume.fv_cluster_count *
        fmp->fm_volume.fv_sectors_per_cluster;
    sfs.f_bfree = fmp->fm_free_clusters *
        fmp->fm_volume.fv_sectors_per_cluster;
    sfs.f_bavail = sfs.f_bfree;
    sfs.f_files = fmp->fm_volume.fv_cluster_count;
    sfs.f_ffree = fmp->fm_free_clusters;
    bcopy(mp->m_mnton, sfs.f_mntonname, MNAMELEN);
    bcopy(mp->m_mntfrom, sfs.f_mntfromname, MNAMELEN);
    return copyout((caddr_t)&sfs, (caddr_t)sbp, sizeof(sfs));
}

static int
fat_sync(struct mount *mp)
{
    struct fat_mount *fmp;
    int error;

    fmp = (struct fat_mount *)mp->m_data;
    if (fmp == 0 || fmp->fm_read_only)
        return 0;
    bflush(fmp->fm_dev);
    error = (*bdevsw[major(fmp->fm_dev)].d_ioctl)(fmp->fm_dev,
        DIOCFLUSH, (caddr_t)0, FWRITE);
    return error == EINVAL || error == EOPNOTSUPP ? 0 : error;
}

static int
fat_unmount(struct mount *mp)
{
    struct fat_mount *fmp;

    fmp = (struct fat_mount *)mp->m_data;
    if (fmp != 0)
        fat_zero(fmp, sizeof(*fmp));
    mp->m_data = 0;
    return 0;
}

static int
fat_mount(struct mount *mp, dev_t dev, int flags, struct inode *ip)
{
    struct fat_mount *fmp;
    struct buf *bp;
    unsigned char *boot;
    daddr_t media_blocks;
    disk_sector_t media_sectors64;
    unsigned media_sectors;
    unsigned i;
    int parsed;
    int error;

    (void)ip;
    fmp = 0;
    for (i = 0; i < NMOUNT; ++i)
        if (!fat_mounts[i].fm_used) {
            fmp = &fat_mounts[i];
            break;
        }
    if (fmp == 0)
        return EMFILE;
    fat_zero(fmp, sizeof(*fmp));
    fmp->fm_used = 1;
    fmp->fm_mount = mp;
    fmp->fm_dev = dev;
    fmp->fm_read_only = (flags & MNT_RDONLY) != 0;
    fmp->fm_next_free = 2u;

    media_sectors64 = 0;
    error = (*bdevsw[major(dev)].d_ioctl)(dev, DIOCGETSECTORS64,
        (caddr_t)&media_sectors64, FREAD);
    if (!error && media_sectors64 > 0xffffffffull) {
        error = EFBIG;
        goto fail;
    }
    media_sectors = (unsigned)media_sectors64;
    if (error || media_sectors == 0) {
        media_blocks = (*bdevsw[major(dev)].d_psize)(dev);
        if (media_blocks <= 0 || (unsigned long)media_blocks > 0x7ffffffful) {
            error = ENXIO;
            goto fail;
        }
        media_sectors = (unsigned)media_blocks << 1;
    }
    fmp->fm_media_sectors = media_sectors;
    fmp->fm_volume.fv_total_sectors = media_sectors;
    error = fat_sector_get(fmp, 0, &bp, &boot);
    if (error)
        goto fail;
    parsed = fat_volume_parse(&fmp->fm_volume, boot, media_sectors);
    brelse(bp);
    if (parsed != FAT_PARSE_OK) {
        error = parsed == FAT_PARSE_UNSUPPORTED ? EOPNOTSUPP : EINVAL;
        goto fail;
    }
    if (fmp->fm_volume.fv_declared_sectors >
        fmp->fm_volume.fv_total_sectors)
        printf("fat: BPB size %u exceeds device %u sectors; "
            "limiting to device\n",
            fmp->fm_volume.fv_declared_sectors,
            fmp->fm_volume.fv_total_sectors);
    if (fmp->fm_volume.fv_total_sectors > FAT_MAX_FILE_SECTORS) {
        error = EFBIG;
        goto fail;
    }
    error = fat_count_free_clusters(fmp);
    if (error)
        goto fail;

    mp->m_data = (caddr_t)fmp;
    mp->m_filsys.fs_ronly = fmp->fm_read_only;
    mp->m_filsys.fs_flags = flags;
    printf("fat%u: FAT%u, %u sectors, %u sectors/cluster%s\n",
        i, fmp->fm_volume.fv_type, fmp->fm_volume.fv_total_sectors,
        fmp->fm_volume.fv_sectors_per_cluster,
        fmp->fm_read_only ? ", read-only" : ", read-write");
    return 0;
fail:
    fat_zero(fmp, sizeof(*fmp));
    return error;
}

static int
fat_namematch(struct mount *mp, const char *name, unsigned namelen,
    const char *entry, unsigned entrylen)
{
    (void)mp;
    return fat_ascii_name_equal(name, namelen, entry, entrylen);
}

struct vfsops fat_vfsops = {
    fat_mount,
    fat_unmount,
    fat_load_inode,
    fat_blkatoff,
    fat_rwip,
    fat_create,
    fat_remove,
    fat_mkdir,
    fat_rmdir,
    fat_rename,
    fat_truncate,
    fat_statfs,
    fat_sync,
    fat_namematch,
    VFSOPS_BLOCK_DEVICE,
};
