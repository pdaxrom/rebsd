/* Host-side tests for transport-independent MBR/GPT parsing. */

#include <stdio.h>
#include <string.h>
#include <disk/disk.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                 \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define TEST_MEDIA_SECTORS          4096u
#define TEST_GPT_ENTRY_BYTES        (128u * 128u)

static void
set_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static void
set_le64(unsigned char *data, disk_sector_t value)
{
    set_le32(data, (unsigned)value);
    set_le32(data + 4, (unsigned)(value >> 32));
}

static void
set_partition(unsigned char *sector, unsigned index, unsigned status,
    unsigned type, unsigned start, unsigned sectors)
{
    unsigned char *entry;

    entry = sector + 446u + index * 16u;
    entry[0] = (unsigned char)status;
    entry[4] = (unsigned char)type;
    set_le32(entry + 8, start);
    set_le32(entry + 12, sectors);
}

static void
make_gpt_header(unsigned char *sector, disk_sector_t media_sectors,
    disk_sector_t header_lba, disk_sector_t entries_lba,
    unsigned entries_crc)
{
    unsigned crc;

    memset(sector, 0, DISK_SECTOR_SIZE);
    memcpy(sector, "EFI PART", 8);
    set_le32(sector + 8, 0x00010000u);
    set_le32(sector + 12, 92u);
    set_le64(sector + 24, header_lba);
    set_le64(sector + 32, header_lba == 1 ? media_sectors - 1 : 1);
    set_le64(sector + 40, 34u);
    set_le64(sector + 48, media_sectors - 34u);
    sector[56] = 0x44;
    set_le64(sector + 72, entries_lba);
    set_le32(sector + 80, 128u);
    set_le32(sector + 84, 128u);
    set_le32(sector + 88, entries_crc);
    crc = disk_crc32(sector, 92u);
    set_le32(sector + 16, crc);
}

static int
test_valid_mbr(void)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    struct disk_mbr mbr;
    struct disk_table table;
    disk_sector_t start;
    disk_sector_t sectors;

    memset(sector, 0, sizeof(sector));
    set_partition(sector, 0, 0x80, 0xb7, 2, 400);
    set_partition(sector, 1, 0, 0x0c, 402, 500);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);
    disk_table_from_mbr(&table, &mbr);

    CHECK(mbr.dm_valid == 1);
    CHECK(!disk_mbr_is_protective(&mbr));
    CHECK(table.dt_scheme == DISK_SCHEME_MBR);
    CHECK(table.dt_partitions[0].dp_status == 0x80);
    CHECK(table.dt_partitions[0].dp_type == 0xb7);
    CHECK(table.dt_partitions[0].dp_offset == 2);
    CHECK(table.dt_partitions[0].dp_nsectors == 400);
    CHECK(table.dt_partitions[1].dp_type == 0x0c);
    CHECK(disk_table_region(&table, TEST_MEDIA_SECTORS,
        DISK_MINOR_WHOLE, &start, &sectors) == 0);
    CHECK(start == 0 && sectors == TEST_MEDIA_SECTORS);
    CHECK(disk_table_region(&table, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(0), &start, &sectors) == 0);
    CHECK(start == 2 && sectors == 400);
    CHECK(disk_table_region(&table, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(2), &start, &sectors) != 0);
    return 0;
}

static int
test_invalid_and_protective_mbr(void)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    struct disk_mbr mbr;
    struct disk_table table;

    memset(sector, 0, sizeof(sector));
    set_partition(sector, 0, 0, 0xb7, 3900, 300);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);
    disk_table_from_mbr(&table, &mbr);
    CHECK(mbr.dm_valid == 1);
    CHECK(mbr.dm_partitions[0].dp_type == 0);
    CHECK(table.dt_scheme == DISK_SCHEME_MBR);

    memset(sector, 0, sizeof(sector));
    set_partition(sector, 0, 0, 0xee, 1, TEST_MEDIA_SECTORS - 1u);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);
    CHECK(disk_mbr_is_protective(&mbr));
    disk_table_from_mbr(&table, &mbr);
    CHECK(table.dt_valid == 0 && table.dt_scheme == DISK_SCHEME_NONE);
    return 0;
}

static int
test_gpt_parser(void)
{
    unsigned char entries[TEST_GPT_ENTRY_BYTES];
    unsigned char header_sector[DISK_SECTOR_SIZE];
    struct disk_gpt_header header;
    struct disk_partition part;
    unsigned entries_crc;

    memset(entries, 0, sizeof(entries));
    entries[0] = 0xa2;
    entries[16] = 0x71;
    set_le64(entries + 32, 34u);
    set_le64(entries + 40, 100u);
    set_le64(entries + 48, 0x100000000ULL);
    entries_crc = disk_crc32(entries, sizeof(entries));
    make_gpt_header(header_sector, TEST_MEDIA_SECTORS, 1, 2, entries_crc);

    CHECK(disk_crc32("123456789", 9) == 0xcbf43926u);
    CHECK(disk_gpt_header_parse(&header, header_sector, 1,
        TEST_MEDIA_SECTORS) == 0);
    CHECK(header.gh_entries_lba == 2);
    CHECK(header.gh_entry_count == 128 && header.gh_entry_size == 128);
    CHECK(header.gh_entries_crc32 == entries_crc);
    CHECK(disk_gpt_entry_parse(&part, entries, &header) == 0);
    CHECK(part.dp_scheme == DISK_SCHEME_GPT);
    CHECK(part.dp_offset == 34 && part.dp_nsectors == 67);
    CHECK(part.dp_attributes == 0x100000000ULL);

    header_sector[20] = 1;
    CHECK(disk_gpt_header_parse(&header, header_sector, 1,
        TEST_MEDIA_SECTORS) != 0);
    header_sector[20] = 0;
    header_sector[16] ^= 1;
    CHECK(disk_gpt_header_parse(&header, header_sector, 1,
        TEST_MEDIA_SECTORS) != 0);
    return 0;
}

static int
test_gpt_64_bit_entry(void)
{
    const disk_sector_t media_sectors = 0x100000400ULL;
    unsigned char entries[TEST_GPT_ENTRY_BYTES];
    unsigned char header_sector[DISK_SECTOR_SIZE];
    struct disk_gpt_header header;
    struct disk_partition part;

    memset(entries, 0, sizeof(entries));
    entries[0] = 1;
    entries[16] = 2;
    set_le64(entries + 32, 0x100000000ULL);
    set_le64(entries + 40, 0x100000063ULL);
    make_gpt_header(header_sector, media_sectors, 1, 2,
        disk_crc32(entries, sizeof(entries)));
    CHECK(disk_gpt_header_parse(&header, header_sector, 1,
        media_sectors) == 0);
    CHECK(disk_gpt_entry_parse(&part, entries, &header) == 0);
    CHECK(part.dp_offset == 0x100000000ULL);
    CHECK(part.dp_nsectors == 100u);
    return 0;
}

static int
test_minor_layout(void)
{
    CHECK(DISK_MINOR(0, DISK_MINOR_WHOLE) == 0);
    CHECK(DISK_MINOR(0, DISK_MINOR_PARTITION(15)) == 16);
    CHECK(DISK_MINOR(1, DISK_MINOR_WHOLE) == 17);
    CHECK(DISK_MINOR_UNIT(67) == 3);
    CHECK(DISK_MINOR_PART(67) == 16);
    return 0;
}

int
main(void)
{
    CHECK(test_valid_mbr() == 0);
    CHECK(test_invalid_and_protective_mbr() == 0);
    CHECK(test_gpt_parser() == 0);
    CHECK(test_gpt_64_bit_entry() == 0);
    CHECK(test_minor_layout() == 0);
    puts("disk_test: all tests passed");
    return 0;
}
