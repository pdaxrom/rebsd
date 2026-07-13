/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DISK_DISK_H_
#define _DISK_DISK_H_

#include <sys/types.h>

#define DISK_SECTOR_SIZE                512u
#define DISK_MBR_PARTITIONS             4u
#define DISK_MINORS_PER_UNIT            (DISK_MBR_PARTITIONS + 1u)
#define DISK_MINOR_WHOLE                0u
#define DISK_MINOR_PARTITION(n)         ((unsigned)(n) + 1u)
#define DISK_MINOR(unit, part)          \
    ((unsigned)(unit) * DISK_MINORS_PER_UNIT + (unsigned)(part))
#define DISK_MINOR_UNIT(n)              \
    ((unsigned)(n) / DISK_MINORS_PER_UNIT)
#define DISK_MINOR_PART(n)              \
    ((unsigned)(n) % DISK_MINORS_PER_UNIT)

#define DISK_FLAG_READ_ONLY             0x0001u
#define DISK_FLAG_REMOVABLE             0x0002u

struct disk_partition {
    unsigned char dp_status;
    unsigned char dp_type;
    unsigned dp_offset;
    unsigned dp_nsectors;
};

struct disk_mbr {
    struct disk_partition dm_partitions[DISK_MBR_PARTITIONS];
    unsigned dm_valid;
};

/*
 * Transport-independent backing store.  Each method returns zero or errno.
 * A backend owns command splitting, DMA/cache handling and media timeouts.
 */
struct disk_backend_ops {
    int (*dbo_read)(void *, unsigned, unsigned, void *);
    int (*dbo_write)(void *, unsigned, unsigned, const void *);
    int (*dbo_flush)(void *);
    int (*dbo_present)(void *);
};

struct disk_attach_args {
    const struct disk_backend_ops *da_ops;
    void *da_arg;
    unsigned da_sector_count;
    unsigned da_sector_size;
    unsigned da_flags;
};

void disk_mbr_parse(struct disk_mbr *, const unsigned char *, unsigned);
int disk_mbr_region(const struct disk_mbr *, unsigned, unsigned, unsigned *,
    unsigned *);

#ifdef KERNEL
struct buf;

int disk_attach(const struct disk_attach_args *, unsigned *);
void disk_detach(unsigned, void *);
void diskattach(int);

int disk_bdev_open(dev_t, int, int);
int disk_bdev_close(dev_t, int, int);
void disk_bdev_strategy(struct buf *);
daddr_t disk_bdev_size(dev_t);
int disk_bdev_ioctl(dev_t, u_int, caddr_t, int);
#endif

#endif /* _DISK_DISK_H_ */
