/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DISK_RAMDISK_H_
#define _DISK_RAMDISK_H_

#include <sys/types.h>
#include <sys/disk.h>

struct buf;
struct ramcomp;
struct ramcomp_stats;
struct vm_page;

#define RAMDISK_F_COMPRESSION    RAMDISK_CONFIG_COMPRESSION

struct ramdisk_config {
    volatile void *rdc_backing;
    unsigned       rdc_backing_bytes;
    unsigned       rdc_media_bytes;
    int            rdc_minor;
    unsigned       rdc_block_shift;
    unsigned       rdc_flags;
    struct ramcomp *rdc_compression;
    void           *rdc_compression_metadata;
    unsigned       rdc_compression_metadata_bytes;
};

/* Writable memory exposed as a block device, optionally compressed. */
struct ramdisk {
    volatile unsigned char *rd_backing;
    unsigned rd_backing_bytes;
    unsigned rd_media_bytes;
    int rd_minor;
    unsigned rd_block_shift;
    unsigned rd_flags;
    struct ramcomp *rd_compression;
};

struct ramdisk_slot {
    struct ramdisk rs_disk;
    unsigned rs_open_count;
    unsigned rs_configured;
    struct ramcomp *rs_compression;
    struct vm_page *rs_backing_pages;
    unsigned rs_backing_page_count;
    struct vm_page *rs_metadata_pages;
    unsigned rs_metadata_page_count;
    void *rs_metadata;
};

struct ramdisk_controller {
    struct ramdisk_slot rc_slot[RAMDISK_MAX_DEVICES];
};

int ramdisk_init(struct ramdisk *, const struct ramdisk_config *);
int ramdisk_bdev_open(const struct ramdisk *, dev_t, int, int);
int ramdisk_bdev_close(const struct ramdisk *, dev_t, int, int);
void ramdisk_bdev_strategy(const struct ramdisk *, struct buf *);
daddr_t ramdisk_bdev_size(const struct ramdisk *, dev_t);
int ramdisk_bdev_ioctl(const struct ramdisk *, dev_t, u_int, caddr_t, int);
void ramdisk_controller_init(struct ramdisk_controller *);
int ramdisk_controller_open(struct ramdisk_controller *, dev_t, int, int);
int ramdisk_controller_close(struct ramdisk_controller *, dev_t, int, int);
void ramdisk_controller_strategy(struct ramdisk_controller *, struct buf *);
daddr_t ramdisk_controller_size(struct ramdisk_controller *, dev_t);
int ramdisk_controller_ioctl(struct ramdisk_controller *, dev_t, u_int,
    caddr_t, int);
int ramdisk_controller_compression_stats(struct ramdisk_controller *, int,
    struct ramcomp_stats *);

#endif /* _DISK_RAMDISK_H_ */
