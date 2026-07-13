/*
 * Simple MBR definitions shared by fdisk and its host-side tests.
 *
 * Keep the on-disk structure byte-oriented: MBR fields are little-endian,
 * while ReBSD is intended to run on machines with more than one byte order.
 */

#ifndef _FDISK_H_
#define _FDISK_H_

#define FDISK_MBR_BYTES             512u
#define FDISK_MBR_BOOTSTRAP_BYTES   446u
#define FDISK_MBR_PARTITIONS        4u
#define FDISK_MBR_SIGNATURE_LO      0x55u
#define FDISK_MBR_SIGNATURE_HI      0xaau
#define FDISK_PARTITION_ACTIVE      0x80u

struct fdisk_partition {
    unsigned char fp_status;
    unsigned char fp_start_chs[3];
    unsigned char fp_type;
    unsigned char fp_end_chs[3];
    unsigned char fp_start_lba[4];
    unsigned char fp_sector_count[4];
} __attribute__((packed));

struct fdisk_mbr {
    unsigned char fm_bootstrap[FDISK_MBR_BOOTSTRAP_BYTES];
    struct fdisk_partition fm_partitions[FDISK_MBR_PARTITIONS];
    unsigned char fm_signature[2];
} __attribute__((packed));

unsigned fdisk_mbr_get_le32(const unsigned char *);
void fdisk_mbr_put_le32(unsigned char *, unsigned);
unsigned fdisk_partition_start(const struct fdisk_partition *);
unsigned fdisk_partition_sectors(const struct fdisk_partition *);
void fdisk_partition_set(struct fdisk_partition *, unsigned char,
    unsigned char, unsigned, unsigned);
void fdisk_partition_clear(struct fdisk_partition *);
int fdisk_mbr_has_signature(const struct fdisk_mbr *);
void fdisk_mbr_initialize(struct fdisk_mbr *);
int fdisk_mbr_validate(const struct fdisk_mbr *, unsigned);

#endif /* _FDISK_H_ */
