/*
 * Machine-independent helpers for the legacy MBR partition table.
 */

#include "fdisk.h"

typedef char fdisk_partition_size_check[
    sizeof(struct fdisk_partition) == 16 ? 1 : -1];
typedef char fdisk_mbr_size_check[
    sizeof(struct fdisk_mbr) == FDISK_MBR_BYTES ? 1 : -1];

static void
fdisk_zero(void *vptr, unsigned length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

unsigned
fdisk_mbr_get_le32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

void
fdisk_mbr_put_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

unsigned
fdisk_partition_start(const struct fdisk_partition *partition)
{
    return fdisk_mbr_get_le32(partition->fp_start_lba);
}

unsigned
fdisk_partition_sectors(const struct fdisk_partition *partition)
{
    return fdisk_mbr_get_le32(partition->fp_sector_count);
}

void
fdisk_partition_clear(struct fdisk_partition *partition)
{
    fdisk_zero(partition, sizeof(*partition));
}

void
fdisk_partition_set(struct fdisk_partition *partition, unsigned char status,
    unsigned char type, unsigned start, unsigned sectors)
{
    fdisk_partition_clear(partition);
    partition->fp_status = status;
    partition->fp_type = type;
    fdisk_mbr_put_le32(partition->fp_start_lba, start);
    fdisk_mbr_put_le32(partition->fp_sector_count, sectors);
}

int
fdisk_mbr_has_signature(const struct fdisk_mbr *mbr)
{
    return mbr->fm_signature[0] == FDISK_MBR_SIGNATURE_LO &&
        mbr->fm_signature[1] == FDISK_MBR_SIGNATURE_HI;
}

void
fdisk_mbr_initialize(struct fdisk_mbr *mbr)
{
    unsigned i;

    for (i = 0; i < FDISK_MBR_PARTITIONS; ++i)
        fdisk_partition_clear(&mbr->fm_partitions[i]);

    /* Preserve the legacy RetroBSD disk-identification bytes. */
    mbr->fm_bootstrap[220] = 0x80;
    mbr->fm_bootstrap[440] = 'R';
    mbr->fm_bootstrap[441] = 'E';
    mbr->fm_bootstrap[442] = 'T';
    mbr->fm_bootstrap[443] = 'R';
    mbr->fm_signature[0] = FDISK_MBR_SIGNATURE_LO;
    mbr->fm_signature[1] = FDISK_MBR_SIGNATURE_HI;
}

int
fdisk_mbr_validate(const struct fdisk_mbr *mbr, unsigned disk_sectors)
{
    const struct fdisk_partition *part;
    const struct fdisk_partition *other;
    unsigned start;
    unsigned sectors;
    unsigned end;
    unsigned other_start;
    unsigned other_sectors;
    unsigned other_end;
    unsigned i;
    unsigned j;

    if (!fdisk_mbr_has_signature(mbr))
        return -1;
    for (i = 0; i < FDISK_MBR_PARTITIONS; ++i) {
        part = &mbr->fm_partitions[i];
        start = fdisk_partition_start(part);
        sectors = fdisk_partition_sectors(part);
        if (part->fp_type == 0 && sectors == 0)
            continue;
        if (part->fp_type == 0 || sectors == 0 || start == 0 ||
            sectors > ~start ||
            (disk_sectors != 0 &&
            (start >= disk_sectors || sectors > disk_sectors - start)))
            return -1;
        end = start + sectors;
        for (j = i + 1; j < FDISK_MBR_PARTITIONS; ++j) {
            other = &mbr->fm_partitions[j];
            other_start = fdisk_partition_start(other);
            other_sectors = fdisk_partition_sectors(other);
            if (other->fp_type == 0 && other_sectors == 0)
                continue;
            if (other->fp_type == 0 || other_sectors == 0 ||
                other_start == 0 || other_sectors > ~other_start)
                return -1;
            other_end = other_start + other_sectors;
            if (start < other_end && other_start < end)
                return -1;
        }
    }
    return 0;
}
