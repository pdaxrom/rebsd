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

#define DISK_GPT_HEADER_MIN_SIZE        92u
#define DISK_GPT_REVISION_1_0           0x00010000u
#define DISK_GPT_ENTRY_MIN_SIZE         128u

static unsigned
disk_get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static disk_sector_t
disk_get_le64(const unsigned char *data)
{
    return (disk_sector_t)disk_get_le32(data) |
        ((disk_sector_t)disk_get_le32(data + 4) << 32);
}

static void
disk_zero(void *arg, size_t length)
{
    unsigned char *data;

    data = (unsigned char *)arg;
    while (length-- != 0)
        *data++ = 0;
}

static void
disk_copy(void *to_arg, const void *from_arg, size_t length)
{
    unsigned char *to;
    const unsigned char *from;

    to = (unsigned char *)to_arg;
    from = (const unsigned char *)from_arg;
    while (length-- != 0)
        *to++ = *from++;
}

static int
disk_guid_is_zero(const unsigned char *guid)
{
    unsigned i;

    for (i = 0; i < 16u; ++i)
        if (guid[i] != 0)
            return 0;
    return 1;
}

void
disk_mbr_parse(struct disk_mbr *mbr, const unsigned char *sector,
    disk_sector_t media_sectors)
{
    struct disk_partition *part;
    const unsigned char *entry;
    unsigned offset;
    unsigned sectors;
    unsigned i;

    if (mbr == 0)
        return;
    disk_zero(mbr, sizeof(*mbr));
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
        if (entry[4] == 0 || sectors == 0 ||
            (disk_sector_t)offset >= media_sectors ||
            (disk_sector_t)sectors > media_sectors - offset)
            continue;
        part = &mbr->dm_partitions[i];
        part->dp_status = entry[0];
        part->dp_type = entry[4];
        part->dp_scheme = DISK_SCHEME_MBR;
        part->dp_offset = offset;
        part->dp_nsectors = sectors;
    }
}

int
disk_mbr_is_protective(const struct disk_mbr *mbr)
{
    unsigned i;

    if (mbr == 0 || !mbr->dm_valid)
        return 0;
    for (i = 0; i < DISK_MBR_PARTITIONS; ++i)
        if (mbr->dm_partitions[i].dp_type == 0xeeu &&
            mbr->dm_partitions[i].dp_offset == 1)
            return 1;
    return 0;
}

void
disk_table_from_mbr(struct disk_table *table, const struct disk_mbr *mbr)
{
    unsigned i;

    if (table == 0)
        return;
    disk_zero(table, sizeof(*table));
    if (mbr == 0 || !mbr->dm_valid || disk_mbr_is_protective(mbr))
        return;
    table->dt_valid = 1;
    table->dt_scheme = DISK_SCHEME_MBR;
    for (i = 0; i < DISK_MBR_PARTITIONS; ++i)
        table->dt_partitions[i] = mbr->dm_partitions[i];
}

int
disk_table_region(const struct disk_table *table, disk_sector_t media_sectors,
    unsigned part_number, disk_sector_t *start_sector,
    disk_sector_t *sector_count)
{
    const struct disk_partition *part;

    if (media_sectors == 0 || start_sector == 0 || sector_count == 0)
        return -1;
    if (part_number == DISK_MINOR_WHOLE) {
        *start_sector = 0;
        *sector_count = media_sectors;
        return 0;
    }
    if (table == 0 || !table->dt_valid || part_number > DISK_PARTITIONS)
        return -1;
    part = &table->dt_partitions[part_number - 1u];
    if (part->dp_scheme == DISK_SCHEME_NONE || part->dp_nsectors == 0 ||
        part->dp_offset >= media_sectors ||
        part->dp_nsectors > media_sectors - part->dp_offset)
        return -1;
    *start_sector = part->dp_offset;
    *sector_count = part->dp_nsectors;
    return 0;
}

unsigned
disk_crc32_begin(void)
{
    return 0xffffffffu;
}

unsigned
disk_crc32_update(unsigned crc, const void *arg, size_t length)
{
    const unsigned char *data;
    unsigned i;

    data = (const unsigned char *)arg;
    while (length-- != 0) {
        crc ^= *data++;
        for (i = 0; i < 8u; ++i)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

unsigned
disk_crc32_end(unsigned crc)
{
    return crc ^ 0xffffffffu;
}

unsigned
disk_crc32(const void *arg, size_t length)
{
    return disk_crc32_end(disk_crc32_update(disk_crc32_begin(), arg,
        length));
}

static unsigned
disk_gpt_header_crc(const unsigned char *sector, unsigned header_size)
{
    static const unsigned char zero_crc[4] = { 0, 0, 0, 0 };
    unsigned crc;

    crc = disk_crc32_begin();
    crc = disk_crc32_update(crc, sector, 16u);
    crc = disk_crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = disk_crc32_update(crc, sector + 20u, header_size - 20u);
    return disk_crc32_end(crc);
}

int
disk_gpt_header_parse(struct disk_gpt_header *header,
    const unsigned char *sector, disk_sector_t header_lba,
    disk_sector_t media_sectors)
{
    disk_sector_t array_sectors;
    disk_sector_t array_bytes;
    unsigned header_size;
    unsigned entry_size;
    unsigned entry_count;

    if (header == 0)
        return -1;
    disk_zero(header, sizeof(*header));
    if (sector == 0 || media_sectors < 3 ||
        (header_lba != 1 && header_lba != media_sectors - 1) ||
        sector[0] != 'E' || sector[1] != 'F' || sector[2] != 'I' ||
        sector[3] != ' ' || sector[4] != 'P' || sector[5] != 'A' ||
        sector[6] != 'R' || sector[7] != 'T' ||
        disk_get_le32(sector + 8) != DISK_GPT_REVISION_1_0)
        return -1;

    header_size = disk_get_le32(sector + 12);
    if (header_size < DISK_GPT_HEADER_MIN_SIZE ||
        header_size > DISK_SECTOR_SIZE || disk_get_le32(sector + 20) != 0 ||
        disk_gpt_header_crc(sector, header_size) !=
        disk_get_le32(sector + 16))
        return -1;

    header->gh_current_lba = disk_get_le64(sector + 24);
    header->gh_alternate_lba = disk_get_le64(sector + 32);
    header->gh_first_usable_lba = disk_get_le64(sector + 40);
    header->gh_last_usable_lba = disk_get_le64(sector + 48);
    disk_copy(header->gh_disk_guid, sector + 56, 16u);
    header->gh_entries_lba = disk_get_le64(sector + 72);
    entry_count = disk_get_le32(sector + 80);
    entry_size = disk_get_le32(sector + 84);
    header->gh_entry_count = entry_count;
    header->gh_entry_size = entry_size;
    header->gh_entries_crc32 = disk_get_le32(sector + 88);

    if (header->gh_current_lba != header_lba ||
        header->gh_alternate_lba !=
        (header_lba == 1 ? media_sectors - 1 : 1) ||
        header->gh_first_usable_lba > header->gh_last_usable_lba ||
        header->gh_last_usable_lba >= media_sectors || entry_count == 0 ||
        entry_count > DISK_GPT_ENTRIES_MAX ||
        entry_size < DISK_GPT_ENTRY_MIN_SIZE ||
        entry_size > DISK_SECTOR_SIZE ||
        (entry_size & (entry_size - 1u)) != 0)
        return -1;

    array_bytes = (disk_sector_t)entry_count * entry_size;
    array_sectors = (array_bytes + DISK_SECTOR_SIZE - 1u) /
        DISK_SECTOR_SIZE;
    if (header->gh_entries_lba >= media_sectors || array_sectors == 0 ||
        array_sectors > media_sectors - header->gh_entries_lba)
        return -1;
    if (header_lba == 1) {
        if (header->gh_entries_lba <= header_lba ||
            header->gh_entries_lba + array_sectors >
            header->gh_first_usable_lba)
            return -1;
    } else if (header->gh_entries_lba <= header->gh_last_usable_lba ||
        header->gh_entries_lba + array_sectors > header_lba) {
        return -1;
    }
    return 0;
}

int
disk_gpt_entry_parse(struct disk_partition *part, const unsigned char *entry,
    const struct disk_gpt_header *header)
{
    disk_sector_t first;
    disk_sector_t last;

    if (part == 0 || entry == 0 || header == 0)
        return -1;
    disk_zero(part, sizeof(*part));
    if (disk_guid_is_zero(entry))
        return 0;
    if (disk_guid_is_zero(entry + 16u))
        return -1;
    first = disk_get_le64(entry + 32u);
    last = disk_get_le64(entry + 40u);
    if (first < header->gh_first_usable_lba ||
        last > header->gh_last_usable_lba || first > last)
        return -1;
    part->dp_scheme = DISK_SCHEME_GPT;
    part->dp_offset = first;
    part->dp_nsectors = last - first + 1u;
    part->dp_attributes = disk_get_le64(entry + 48u);
    disk_copy(part->dp_type_guid, entry, 16u);
    disk_copy(part->dp_unique_guid, entry + 16u, 16u);
    return 0;
}
