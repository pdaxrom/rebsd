/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <disk/disk.h>

#define DISK_MBR_PARTITION_OFFSET       446u
#define DISK_MBR_PARTITION_SIZE         16u
#define DISK_MBR_SIGNATURE_OFFSET       510u

static unsigned
disk_get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void
disk_mbr_zero(struct disk_mbr *mbr)
{
    unsigned char *data;
    size_t length;

    data = (unsigned char *)mbr;
    length = sizeof(*mbr);
    while (length-- != 0)
        *data++ = 0;
}

void
disk_mbr_parse(struct disk_mbr *mbr, const unsigned char *sector,
    unsigned media_sectors)
{
    struct disk_partition *part;
    const unsigned char *entry;
    unsigned offset;
    unsigned sectors;
    unsigned i;

    if (mbr == 0)
        return;
    disk_mbr_zero(mbr);
    if (sector == 0 || media_sectors == 0 ||
        sector[DISK_MBR_SIGNATURE_OFFSET] != 0x55u ||
        sector[DISK_MBR_SIGNATURE_OFFSET + 1u] != 0xaau)
        return;
    mbr->dm_valid = 1;
    for (i = 0; i < DISK_MBR_PARTITIONS; ++i) {
        entry = sector + DISK_MBR_PARTITION_OFFSET +
            i * DISK_MBR_PARTITION_SIZE;
        offset = disk_get_le32(entry + 8);
        sectors = disk_get_le32(entry + 12);
        if (entry[4] == 0 || sectors == 0 || offset >= media_sectors ||
            sectors > media_sectors - offset)
            continue;
        part = &mbr->dm_partitions[i];
        part->dp_status = entry[0];
        part->dp_type = entry[4];
        part->dp_offset = offset;
        part->dp_nsectors = sectors;
    }
}

int
disk_mbr_region(const struct disk_mbr *mbr, unsigned media_sectors,
    unsigned part_number, unsigned *start_sector, unsigned *sector_count)
{
    const struct disk_partition *part;

    if (mbr == 0 || media_sectors == 0 || start_sector == 0 ||
        sector_count == 0)
        return -1;
    if (part_number == DISK_MINOR_WHOLE) {
        *start_sector = 0;
        *sector_count = media_sectors;
        return 0;
    }
    if (!mbr->dm_valid || part_number > DISK_MBR_PARTITIONS)
        return -1;
    part = &mbr->dm_partitions[part_number - 1u];
    if (part->dp_type == 0 || part->dp_nsectors == 0)
        return -1;
    *start_sector = part->dp_offset;
    *sector_count = part->dp_nsectors;
    return 0;
}
