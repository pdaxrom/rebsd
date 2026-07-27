/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DISK_ROMDISK_H_
#define _DISK_ROMDISK_H_

#include <sys/types.h>

struct buf;

struct romdisk_ops {
    int (*rd_media)(void *, unsigned *);
    int (*rd_read)(void *, unsigned, void *, unsigned);
};

/*
 * Read-only block device.  A directly linked image uses rd_start/rd_end.
 * Hardware which cannot directly address its image supplies rd_ops instead.
 */
struct romdisk {
    const unsigned char *rd_start;
    const unsigned char *rd_end;
    int rd_minor;
    unsigned rd_block_shift;
    const struct romdisk_ops *rd_ops;
    void *rd_cookie;
};

int romdisk_bdev_open(const struct romdisk *, dev_t, int, int);
int romdisk_bdev_close(const struct romdisk *, dev_t, int, int);
void romdisk_bdev_strategy(const struct romdisk *, struct buf *);
daddr_t romdisk_bdev_size(const struct romdisk *, dev_t);
int romdisk_bdev_ioctl(const struct romdisk *, dev_t, u_int, caddr_t, int);

#if defined(KERNEL) && !defined(DISK_HOST_TEST)
const struct romdisk *romdisk_md_device(void);
int romdisk_open(dev_t, int, int);
int romdisk_close(dev_t, int, int);
void romdisk_strategy(struct buf *);
daddr_t romdisk_size(dev_t);
int romdisk_ioctl(dev_t, u_int, caddr_t, int);
#endif

#endif /* _DISK_ROMDISK_H_ */
