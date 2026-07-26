/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common read-only memory backend for embedded block-device images. */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/disk.h>
#include <disk/disk.h>

static int
disk_memory_read(void *arg, disk_sector_t sector, unsigned count, void *data)
{
    struct disk_memory *memory;
    unsigned char *destination;
    const unsigned char *source;
    size_t bytes;

    memory = (struct disk_memory *)arg;
    if (memory == 0 || memory->dm_data == 0 || data == 0 ||
        count > memory->dm_bytes / DISK_SECTOR_SIZE ||
        sector > memory->dm_bytes / DISK_SECTOR_SIZE - count)
        return EIO;
    source = memory->dm_data + (size_t)sector * DISK_SECTOR_SIZE;
    destination = (unsigned char *)data;
    bytes = (size_t)count * DISK_SECTOR_SIZE;
    while (bytes-- != 0)
        *destination++ = *source++;
    return 0;
}

static int
disk_memory_present(void *arg)
{
    struct disk_memory *memory;

    memory = (struct disk_memory *)arg;
    return memory != 0 && memory->dm_data != 0 &&
        memory->dm_bytes >= DISK_SECTOR_SIZE;
}

static const struct disk_backend_ops disk_memory_ops = {
    disk_memory_read,
    0,
    0,
    disk_memory_present
};

int
disk_memory_attach(struct disk_memory *memory, const void *data, size_t bytes,
    unsigned *unitp)
{
    struct disk_attach_args args;
    unsigned char *zero;
    size_t remaining;

    if (memory == 0 || data == 0 || bytes < DISK_SECTOR_SIZE ||
        bytes % DISK_SECTOR_SIZE != 0)
        return EINVAL;

    zero = (unsigned char *)&args;
    remaining = sizeof(args);
    while (remaining-- != 0)
        *zero++ = 0;
    memory->dm_data = (const unsigned char *)data;
    memory->dm_bytes = bytes;
    args.da_ops = &disk_memory_ops;
    args.da_arg = memory;
    args.da_sector_count = bytes / DISK_SECTOR_SIZE;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY;
    return disk_attach(&args, unitp);
}
