/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _FS_FAT_FAT_H_
#define _FS_FAT_FAT_H_

#include <sys/types.h>

#define FAT_SECTOR_SIZE             512u
#define FAT_DIRENT_SIZE             32u
#define FAT_DIRENTS_PER_SECTOR      (FAT_SECTOR_SIZE / FAT_DIRENT_SIZE)

#define FAT_TYPE_16                 16u
#define FAT_TYPE_32                 32u

#define FAT_ATTR_READ_ONLY          0x01u
#define FAT_ATTR_HIDDEN             0x02u
#define FAT_ATTR_SYSTEM             0x04u
#define FAT_ATTR_VOLUME_ID          0x08u
#define FAT_ATTR_DIRECTORY          0x10u
#define FAT_ATTR_ARCHIVE            0x20u
#define FAT_ATTR_LONG_NAME          0x0fu

#define FAT_DIRENT_DELETED          0xe5u
#define FAT_DIRENT_END              0x00u
#define FAT_CLUSTER_FREE            0u

#define FAT_PARSE_OK                0
#define FAT_PARSE_INVALID           (-1)
#define FAT_PARSE_UNSUPPORTED       (-2)

struct fat_volume {
    unsigned fv_type;
    unsigned fv_total_sectors;
    unsigned fv_reserved_sectors;
    unsigned fv_fat_start;
    unsigned fv_fat_sectors;
    unsigned fv_data_start;
    unsigned fv_root_dir_start;
    unsigned fv_root_dir_sectors;
    unsigned fv_root_cluster;
    unsigned fv_cluster_count;
    unsigned fv_max_cluster;
    unsigned fv_sectors_per_cluster;
    unsigned fv_fat_count;
    unsigned fv_active_fat;
    unsigned fv_fat_mirrored;
};

struct fat_dirent {
    unsigned fd_attr;
    unsigned fd_ntres;
    unsigned fd_cluster;
    unsigned fd_size;
    unsigned fd_create_time;
    unsigned fd_create_date;
    unsigned fd_access_date;
    unsigned fd_modify_time;
    unsigned fd_modify_date;
};

int fat_volume_parse(struct fat_volume *, const unsigned char *, unsigned);
int fat_cluster_valid(const struct fat_volume *, unsigned);
unsigned fat_cluster_first_sector(const struct fat_volume *, unsigned);
int fat_fat_position(const struct fat_volume *, unsigned, unsigned *,
    unsigned *);
unsigned fat_fat_decode(const struct fat_volume *, const unsigned char *);
void fat_fat_encode(const struct fat_volume *, unsigned char *, unsigned);
int fat_cluster_is_eoc(const struct fat_volume *, unsigned);
int fat_cluster_is_bad(const struct fat_volume *, unsigned);

void fat_dirent_parse(struct fat_dirent *, const unsigned char *);
int fat_dirent_is_visible(const unsigned char *);
unsigned fat_lfn_checksum(const unsigned char *);
int fat_short_name(const unsigned char *, char *, unsigned);
int fat_short_name_encode(const char *, unsigned, unsigned char *);
void fat_dirent_encode(unsigned char *, const unsigned char *, unsigned,
    unsigned, unsigned);
void fat_dirent_set_cluster_size(unsigned char *, unsigned, unsigned);
void fat_directory_encode(unsigned char *, unsigned, unsigned);
int fat_ascii_name_equal(const char *, unsigned, const char *, unsigned);

#ifdef KERNEL
struct vfsops;
extern struct vfsops fat_vfsops;
void fatattach(int);
#endif

#endif /* _FS_FAT_FAT_H_ */
