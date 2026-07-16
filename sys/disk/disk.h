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
#include <sys/disk.h>

#ifndef _DISK_SECTOR_T_DEFINED
#define _DISK_SECTOR_T_DEFINED
typedef unsigned long long disk_sector_t;
#endif
#ifndef DISK_SCHEME_NONE
#define DISK_SCHEME_NONE                0
#define DISK_SCHEME_MBR                 1
#define DISK_SCHEME_GPT                 2
#endif

#define DISK_SECTOR_SIZE                512u
#define DISK_MBR_PARTITIONS             4u
#define DISK_PARTITIONS                 16u
#define DISK_GPT_ENTRIES_MAX            128u
#define DISK_MINORS_PER_UNIT            (DISK_PARTITIONS + 1u)
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
    unsigned char dp_type_guid[16];
    unsigned char dp_unique_guid[16];
    unsigned dp_scheme;
    disk_sector_t dp_offset;
    disk_sector_t dp_nsectors;
    disk_sector_t dp_attributes;
};

struct disk_mbr {
    struct disk_partition dm_partitions[DISK_MBR_PARTITIONS];
    unsigned dm_valid;
};

struct disk_table {
    struct disk_partition dt_partitions[DISK_PARTITIONS];
    unsigned dt_valid;
    unsigned dt_scheme;
    unsigned dt_from_backup;
};

struct disk_gpt_header {
    disk_sector_t gh_current_lba;
    disk_sector_t gh_alternate_lba;
    disk_sector_t gh_first_usable_lba;
    disk_sector_t gh_last_usable_lba;
    disk_sector_t gh_entries_lba;
    unsigned gh_entry_count;
    unsigned gh_entry_size;
    unsigned gh_entries_crc32;
    unsigned char gh_disk_guid[16];
};

/*
 * Transport-independent backing store.  Each method returns zero or errno.
 * A backend owns command splitting, DMA/cache handling and media timeouts.
 */
struct disk_backend_ops {
    int (*dbo_read)(void *, disk_sector_t, unsigned, void *);
    int (*dbo_write)(void *, disk_sector_t, unsigned, const void *);
    int (*dbo_flush)(void *);
    int (*dbo_present)(void *);
};

struct disk_attach_args {
    const struct disk_backend_ops *da_ops;
    void *da_arg;
    disk_sector_t da_sector_count;
    unsigned da_sector_size;
    unsigned da_flags;
};

void disk_mbr_parse(struct disk_mbr *, const unsigned char *, disk_sector_t);
int disk_mbr_is_protective(const struct disk_mbr *);
void disk_table_from_mbr(struct disk_table *, const struct disk_mbr *);
int disk_table_region(const struct disk_table *, disk_sector_t, unsigned,
    disk_sector_t *, disk_sector_t *);

unsigned disk_crc32_begin(void);
unsigned disk_crc32_update(unsigned, const void *, size_t);
unsigned disk_crc32_end(unsigned);
unsigned disk_crc32(const void *, size_t);
int disk_gpt_header_parse(struct disk_gpt_header *, const unsigned char *,
    disk_sector_t, disk_sector_t);
int disk_gpt_entry_parse(struct disk_partition *, const unsigned char *,
    const struct disk_gpt_header *);

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
