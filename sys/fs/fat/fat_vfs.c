/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Read-only FAT16/FAT32 VFS glue.  The backing store is any block device. */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/dir.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/uio.h>
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

struct fat_mount {
    int fm_used;
    struct mount *fm_mount;
    dev_t fm_dev;
    unsigned fm_media_sectors;
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
    printf("fat: read-only FAT16/FAT32 filesystem ready\n");
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

static int
fat_leap_year(unsigned year)
{
    return (year % 4u) == 0 &&
        ((year % 100u) != 0 || (year % 400u) == 0);
}

static time_t
fat_timestamp(unsigned date, unsigned clock)
{
    static const unsigned char month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    unsigned year, month, day, hour, minute, second;
    unsigned days, y, m, limit;
    long seconds, adjustment;

    year = 1980u + ((date >> 9) & 0x7fu);
    month = (date >> 5) & 0x0fu;
    day = date & 0x1fu;
    hour = (clock >> 11) & 0x1fu;
    minute = (clock >> 5) & 0x3fu;
    second = (clock & 0x1fu) * 2u;
    if (month == 0 || month > 12u || day == 0 || hour > 23u ||
        minute > 59u || second > 59u)
        return 0;
    limit = month_days[month - 1u];
    if (month == 2u && fat_leap_year(year))
        ++limit;
    if (day > limit)
        return 0;

    days = 0;
    for (y = 1970u; y < year; ++y)
        days += fat_leap_year(y) ? 366u : 365u;
    for (m = 1; m < month; ++m) {
        days += month_days[m - 1u];
        if (m == 2u && fat_leap_year(year))
            ++days;
    }
    days += day - 1u;
    seconds = (long)hour * 3600l + (long)minute * 60l + (long)second;
    if (days > 0x7ffffffful / 86400ul ||
        (days == 0x7ffffffful / 86400ul &&
        (unsigned long)seconds > 0x7ffffffful % 86400ul))
        return (time_t)0x7fffffffl;
    seconds += (long)days * 86400l;
    if (tz.tz_minuteswest < -1440 || tz.tz_minuteswest > 1440)
        adjustment = 0;
    else
        adjustment = (long)tz.tz_minuteswest * 60l;
    if (adjustment > 0 && seconds > 0x7fffffffl - adjustment)
        return (time_t)0x7fffffffl;
    if (adjustment < 0 && seconds < -adjustment)
        return 0;
    return (time_t)(seconds + adjustment);
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
        ip->i_mode = IFDIR | 0555;
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
    if (dirent.fd_size > 0x7fffffffu)
        return EFBIG;
    if (dirent.fd_size != 0 &&
        !fat_cluster_valid(&fmp->fm_volume, dirent.fd_cluster))
        return EIO;
    ip->i_mode = IFREG | 0444;
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

static int
fat_rwip(struct inode *ip, struct uio *uio, int ioflag)
{
    int type;

    (void)ioflag;
    if (uio->uio_rw != UIO_READ)
        return EROFS;
    type = ip->i_mode & IFMT;
    if (type == IFDIR)
        return fat_read_directory(ip, uio);
    if (type == IFREG)
        return fat_read_file(ip, uio);
    return EFTYPE;
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
    sfs.f_blocks = fmp->fm_volume.fv_total_sectors -
        fmp->fm_volume.fv_data_start;
    sfs.f_bfree = 0;
    sfs.f_bavail = 0;
    sfs.f_files = fmp->fm_volume.fv_cluster_count;
    sfs.f_ffree = 0;
    bcopy(mp->m_mnton, sfs.f_mntonname, MNAMELEN);
    bcopy(mp->m_mntfrom, sfs.f_mntfromname, MNAMELEN);
    return copyout((caddr_t)&sfs, (caddr_t)sbp, sizeof(sfs));
}

static int
fat_sync(struct mount *mp)
{
    (void)mp;
    return 0;
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

    media_sectors = 0;
    error = (*bdevsw[major(dev)].d_ioctl)(dev, DIOCGETSECTORS,
        (caddr_t)&media_sectors, FREAD);
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
    if (fmp->fm_volume.fv_total_sectors > FAT_MAX_FILE_SECTORS) {
        error = EFBIG;
        goto fail;
    }

    mp->m_data = (caddr_t)fmp;
    mp->m_filsys.fs_ronly = 1;
    mp->m_filsys.fs_flags = flags | MNT_RDONLY;
    printf("fat%u: FAT%u, %u sectors, %u sectors/cluster, read-only\n",
        i, fmp->fm_volume.fv_type, fmp->fm_volume.fv_total_sectors,
        fmp->fm_volume.fv_sectors_per_cluster);
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
    0,
    0,
    0,
    0,
    0,
    0,
    fat_statfs,
    fat_sync,
    fat_namematch,
    VFSOPS_BLOCK_DEVICE | VFSOPS_READ_ONLY,
};
