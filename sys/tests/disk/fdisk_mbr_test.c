/* Host-side tests for the fdisk on-disk ABI and validation. */

#include <stdio.h>
#include <string.h>
#include "fdisk.h"

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                 \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static int
test_layout_and_endian(void)
{
    struct fdisk_mbr mbr;
    struct fdisk_partition *part;

    memset(&mbr, 0, sizeof(mbr));
    fdisk_mbr_initialize(&mbr);
    CHECK(sizeof(mbr) == FDISK_MBR_BYTES);
    CHECK(fdisk_mbr_has_signature(&mbr));
    part = &mbr.fm_partitions[0];
    fdisk_partition_set(part, FDISK_PARTITION_ACTIVE, 0xb7,
        0x01020304u, 0x11223344u);
    CHECK(part->fp_start_lba[0] == 0x04);
    CHECK(part->fp_start_lba[3] == 0x01);
    CHECK(part->fp_sector_count[0] == 0x44);
    CHECK(part->fp_sector_count[3] == 0x11);
    CHECK(fdisk_partition_start(part) == 0x01020304u);
    CHECK(fdisk_partition_sectors(part) == 0x11223344u);
    return 0;
}

static int
test_validation(void)
{
    struct fdisk_mbr mbr;

    memset(&mbr, 0, sizeof(mbr));
    fdisk_mbr_initialize(&mbr);
    fdisk_partition_set(&mbr.fm_partitions[0], 0, 0xb7, 2, 400);
    fdisk_partition_set(&mbr.fm_partitions[1], 0, 0x0c, 500, 400);
    CHECK(fdisk_mbr_validate(&mbr, 1000) == 0);

    fdisk_partition_set(&mbr.fm_partitions[1], 0, 0x0c, 300, 400);
    CHECK(fdisk_mbr_validate(&mbr, 1000) != 0);
    fdisk_partition_set(&mbr.fm_partitions[1], 0, 0x0c, 900, 200);
    CHECK(fdisk_mbr_validate(&mbr, 1000) != 0);
    fdisk_partition_clear(&mbr.fm_partitions[1]);
    mbr.fm_signature[0] = 0;
    CHECK(fdisk_mbr_validate(&mbr, 1000) != 0);
    return 0;
}

int
main(void)
{
    CHECK(test_layout_and_endian() == 0);
    CHECK(test_validation() == 0);
    puts("fdisk_mbr_test: all tests passed");
    return 0;
}
