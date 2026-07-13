/* Host-side tests for the transport-independent disk/MBR layer. */

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

#define TEST_MEDIA_SECTORS          1000u

static void
set_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
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

static int
test_valid_mbr(void)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    struct disk_mbr mbr;
    unsigned start;
    unsigned sectors;

    memset(sector, 0, sizeof(sector));
    set_partition(sector, 0, 0x80, 0xb7, 2, 400);
    set_partition(sector, 1, 0, 0x0c, 402, 500);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);

    CHECK(mbr.dm_valid == 1);
    CHECK(mbr.dm_partitions[0].dp_status == 0x80);
    CHECK(mbr.dm_partitions[0].dp_type == 0xb7);
    CHECK(mbr.dm_partitions[0].dp_offset == 2);
    CHECK(mbr.dm_partitions[0].dp_nsectors == 400);
    CHECK(mbr.dm_partitions[1].dp_type == 0x0c);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS, DISK_MINOR_WHOLE,
        &start, &sectors) == 0);
    CHECK(start == 0 && sectors == TEST_MEDIA_SECTORS);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(0), &start, &sectors) == 0);
    CHECK(start == 2 && sectors == 400);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(2), &start, &sectors) != 0);
    return 0;
}

static int
test_invalid_entries_are_hidden(void)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    struct disk_mbr mbr;
    unsigned start;
    unsigned sectors;

    memset(sector, 0, sizeof(sector));
    set_partition(sector, 0, 0, 0xb7, 900, 200);
    set_partition(sector, 1, 0, 0x83, 10, 0);
    sector[510] = 0x55;
    sector[511] = 0xaa;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);
    CHECK(mbr.dm_valid == 1);
    CHECK(mbr.dm_partitions[0].dp_type == 0);
    CHECK(mbr.dm_partitions[1].dp_type == 0);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(0), &start, &sectors) != 0);

    sector[510] = 0;
    disk_mbr_parse(&mbr, sector, TEST_MEDIA_SECTORS);
    CHECK(mbr.dm_valid == 0);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS, DISK_MINOR_WHOLE,
        &start, &sectors) == 0);
    CHECK(disk_mbr_region(&mbr, TEST_MEDIA_SECTORS,
        DISK_MINOR_PARTITION(0), &start, &sectors) != 0);
    return 0;
}

static int
test_minor_layout(void)
{
    CHECK(DISK_MINOR(0, DISK_MINOR_WHOLE) == 0);
    CHECK(DISK_MINOR(0, DISK_MINOR_PARTITION(3)) == 4);
    CHECK(DISK_MINOR(1, DISK_MINOR_WHOLE) == 5);
    CHECK(DISK_MINOR_UNIT(19) == 3);
    CHECK(DISK_MINOR_PART(19) == 4);
    return 0;
}

int
main(void)
{
    CHECK(test_valid_mbr() == 0);
    CHECK(test_invalid_entries_are_hidden() == 0);
    CHECK(test_minor_layout() == 0);
    puts("disk_test: all tests passed");
    return 0;
}
