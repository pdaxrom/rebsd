/* Host-side lifecycle tests for removable common block disks. */

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <disk/disk.h>
#include <disk/ramdisk.h>
#include <disk/romdisk.h>

#define CHECK(expr) do {                                                \
    if (!(expr))                                                        \
        return __LINE__;                                                \
} while (0)

#define TEST_SECTORS               64u
#define TEST_ROMDISK_BLOCK_BYTES   1024u
#define TEST_GPT_ENTRY_BYTES       (128u * 128u)
#define TEST_GPT_MEDIA_SECTORS     0x100000400ULL
#define TEST_GPT_PARTITION_LBA     0x100000000ULL

struct fake_media {
    unsigned char data[TEST_SECTORS * DISK_SECTOR_SIZE];
    int present;
    int write_error;
    int flush_error;
    unsigned write_count;
    unsigned flush_count;
    unsigned read_count;
    unsigned phys_read_count;
    unsigned phys_write_count;
    disk_sector_t last_write_sector;
    unsigned last_write_count;
    disk_sector_t last_read_sector;
    unsigned last_read_count;
};

struct fake_gpt_media {
    unsigned char mbr[DISK_SECTOR_SIZE];
    unsigned char primary[DISK_SECTOR_SIZE];
    unsigned char backup[DISK_SECTOR_SIZE];
    unsigned char entries[TEST_GPT_ENTRY_BYTES];
    disk_sector_t sectors;
    disk_sector_t last_read_lba;
    int present;
};

int puts(const char *);

static void
test_zero(void *arg, size_t length)
{
    unsigned char *data;

    data = (unsigned char *)arg;
    while (length-- != 0)
        *data++ = 0;
}

static void
test_copy(void *to_arg, const void *from_arg, size_t length)
{
    unsigned char *to;
    const unsigned char *from;

    to = (unsigned char *)to_arg;
    from = (const unsigned char *)from_arg;
    while (length-- != 0)
        *to++ = *from++;
}

void
bcopy(const void *from, void *to, size_t length)
{
    test_copy(to, from, length);
}

static int
test_equal(const void *left_arg, const void *right_arg, size_t length)
{
    const unsigned char *left;
    const unsigned char *right;

    left = (const unsigned char *)left_arg;
    right = (const unsigned char *)right_arg;
    while (length-- != 0)
        if (*left++ != *right++)
            return 0;
    return 1;
}

void
biodone(struct buf *bp)
{
    bp->b_flags |= B_DONE;
}

static int
fake_read(void *arg, disk_sector_t sector, unsigned count, void *data)
{
    struct fake_media *media;

    media = (struct fake_media *)arg;
    if (!media->present || count > TEST_SECTORS ||
        sector > TEST_SECTORS - count)
        return EIO;
    ++media->read_count;
    media->last_read_sector = sector;
    media->last_read_count = count;
    test_copy(data, media->data + (unsigned)sector * DISK_SECTOR_SIZE,
        count * DISK_SECTOR_SIZE);
    return 0;
}

static int
fake_write(void *arg, disk_sector_t sector, unsigned count,
    const void *data)
{
    struct fake_media *media;

    media = (struct fake_media *)arg;
    if (!media->present || count > TEST_SECTORS ||
        sector > TEST_SECTORS - count)
        return EIO;
    if (media->write_error != 0)
        return media->write_error;
    test_copy(media->data + (unsigned)sector * DISK_SECTOR_SIZE, data,
        count * DISK_SECTOR_SIZE);
    ++media->write_count;
    media->last_write_sector = sector;
    media->last_write_count = count;
    return 0;
}

static int
fake_flush(void *arg)
{
    struct fake_media *media;

    media = (struct fake_media *)arg;
    if (!media->present)
        return ENXIO;
    ++media->flush_count;
    return media->flush_error;
}

static int
fake_present(void *arg)
{
    return ((struct fake_media *)arg)->present;
}

static int
fake_read_phys(void *arg, disk_sector_t sector, unsigned count, void *data)
{
    ++((struct fake_media *)arg)->phys_read_count;
    return fake_read(arg, sector, count, data);
}

static int
fake_write_phys(void *arg, disk_sector_t sector, unsigned count,
    const void *data)
{
    ++((struct fake_media *)arg)->phys_write_count;
    return fake_write(arg, sector, count, data);
}

static const struct disk_backend_ops fake_ops = {
    fake_read,
    0,
    0,
    fake_present,
    0,
    0
};

static const struct disk_backend_ops fake_writable_ops = {
    fake_read,
    fake_write,
    fake_flush,
    fake_present,
    fake_read_phys,
    fake_write_phys
};

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
make_gpt_header(unsigned char *sector, disk_sector_t media_sectors,
    disk_sector_t header_lba, disk_sector_t entries_lba,
    unsigned entries_crc)
{
    unsigned crc;

    test_zero(sector, DISK_SECTOR_SIZE);
    test_copy(sector, "EFI PART", 8u);
    set_le32(sector + 8, 0x00010000u);
    set_le32(sector + 12, 92u);
    set_le64(sector + 24, header_lba);
    set_le64(sector + 32, header_lba == 1 ? media_sectors - 1 : 1);
    set_le64(sector + 40, 34u);
    set_le64(sector + 48, media_sectors - 34u);
    sector[56] = 0x42;
    set_le64(sector + 72, entries_lba);
    set_le32(sector + 80, 128u);
    set_le32(sector + 84, 128u);
    set_le32(sector + 88, entries_crc);
    crc = disk_crc32(sector, 92u);
    set_le32(sector + 16, crc);
}

static void
fake_gpt_init(struct fake_gpt_media *media)
{
    unsigned entries_crc;

    test_zero(media, sizeof(*media));
    media->present = 1;
    media->sectors = TEST_GPT_MEDIA_SECTORS;
    media->mbr[446 + 4] = 0xee;
    set_le32(media->mbr + 446 + 8, 1u);
    set_le32(media->mbr + 446 + 12, 0xffffffffu);
    media->mbr[510] = 0x55;
    media->mbr[511] = 0xaa;
    media->entries[0] = 0xa2;
    media->entries[16] = 0x71;
    set_le64(media->entries + 32, TEST_GPT_PARTITION_LBA);
    set_le64(media->entries + 40, TEST_GPT_PARTITION_LBA + 99u);
    entries_crc = disk_crc32(media->entries, sizeof(media->entries));
    make_gpt_header(media->primary, media->sectors, 1, 2, entries_crc);
    make_gpt_header(media->backup, media->sectors, media->sectors - 1u,
        media->sectors - 33u, entries_crc);
}

static int
fake_gpt_read(void *arg, disk_sector_t lba, unsigned count, void *data)
{
    struct fake_gpt_media *media;
    disk_sector_t array_lba;

    media = (struct fake_gpt_media *)arg;
    if (!media->present || count != 1 || lba >= media->sectors)
        return EIO;
    media->last_read_lba = lba;
    if (lba == 0)
        test_copy(data, media->mbr, DISK_SECTOR_SIZE);
    else if (lba == 1)
        test_copy(data, media->primary, DISK_SECTOR_SIZE);
    else if (lba == media->sectors - 1u)
        test_copy(data, media->backup, DISK_SECTOR_SIZE);
    else if (lba >= 2 && lba < 34u)
        test_copy(data, media->entries + (unsigned)(lba - 2u) *
            DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    else {
        array_lba = media->sectors - 33u;
        if (lba >= array_lba && lba < media->sectors - 1u)
            test_copy(data, media->entries + (unsigned)(lba - array_lba) *
                DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
        else if (lba >= TEST_GPT_PARTITION_LBA &&
            lba < TEST_GPT_PARTITION_LBA + 100u)
            test_zero(data, DISK_SECTOR_SIZE);
        else
            return EIO;
    }
    return 0;
}

static int
fake_gpt_present(void *arg)
{
    return ((struct fake_gpt_media *)arg)->present;
}

static const struct disk_backend_ops fake_gpt_ops = {
    fake_gpt_read,
    0,
    0,
    fake_gpt_present,
    0,
    0
};

static int
fake_gpt_attach(struct fake_gpt_media *media, unsigned *unitp)
{
    struct disk_attach_args args;

    test_zero(&args, sizeof(args));
    args.da_ops = &fake_gpt_ops;
    args.da_arg = media;
    args.da_sector_count = media->sectors;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY;
    return disk_attach(&args, unitp);
}

static void
fake_init(struct fake_media *media, unsigned pattern)
{
    unsigned i;

    test_zero(media, sizeof(*media));
    media->present = 1;
    for (i = 0; i < sizeof(media->data); ++i)
        media->data[i] = (unsigned char)(i + pattern);
    media->data[510] = 0x55;
    media->data[511] = 0xaa;
}

static int
fake_attach_class(struct fake_media *media, unsigned disk_class,
    unsigned *unitp)
{
    struct disk_attach_args args;

    test_zero(&args, sizeof(args));
    args.da_ops = &fake_ops;
    args.da_arg = media;
    args.da_sector_count = TEST_SECTORS;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY | DISK_FLAG_REMOVABLE;
    args.da_class = disk_class;
    args.da_read_ahead_sectors = 16u;
    return disk_attach(&args, unitp);
}

static int
fake_attach(struct fake_media *media, unsigned *unitp)
{
    return fake_attach_class(media, DISK_CLASS_SD, unitp);
}

static int
fake_writable_attach(struct fake_media *media, unsigned *unitp)
{
    struct disk_attach_args args;

    test_zero(&args, sizeof(args));
    args.da_ops = &fake_writable_ops;
    args.da_arg = media;
    args.da_sector_count = TEST_SECTORS;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_REMOVABLE;
    args.da_read_ahead_sectors = 16u;
    return disk_attach(&args, unitp);
}

static int
fake_write_back_attach(struct fake_media *media, unsigned *unitp)
{
    struct disk_attach_args args;

    test_zero(&args, sizeof(args));
    args.da_ops = &fake_writable_ops;
    args.da_arg = media;
    args.da_sector_count = TEST_SECTORS;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_REMOVABLE;
    args.da_read_ahead_sectors = 16u;
    args.da_write_back_sectors = 16u;
    return disk_attach(&args, unitp);
}

static int
test_open_detach_reuse(void)
{
    struct fake_media first, second, third;
    struct buf bp;
    unsigned char data[DISK_SECTOR_SIZE];
    unsigned unit;
    dev_t dev0;

    fake_init(&first, 1);
    fake_init(&second, 2);
    fake_init(&third, 3);
    diskattach(0);

    CHECK(fake_attach(&first, &unit) == 0 && unit == 0);
    dev0 = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    CHECK(disk_bdev_open(dev0, FWRITE, 0) == EROFS);
    CHECK(disk_bdev_open(dev0, FREAD, 0) == 0);

    disk_detach(0, &first);
    first.present = 0;
    CHECK(disk_bdev_size(dev0) == 0);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev0;
    bp.b_blkno = 0;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == (B_DONE | B_ERROR));
    CHECK(bp.b_resid == sizeof(data));

    /* The disconnected open unit stays reserved for its stale mount. */
    CHECK(fake_attach(&second, &unit) == 0 && unit == 1);
    CHECK(disk_bdev_close(dev0, FREAD, 0) == 0);
    CHECK(disk_bdev_close(dev0, FREAD, 0) == ENXIO);

    disk_detach(1, &second);
    second.present = 0;
    CHECK(fake_attach(&third, &unit) == 0 && unit == 0);
    disk_detach(0, &third);
    return 0;
}

static int
test_disk_class_namespaces(void)
{
    struct fake_media sd_media, wd_media;
    struct buf bp;
    unsigned char sd_data[DISK_SECTOR_SIZE];
    unsigned char wd_data[DISK_SECTOR_SIZE];
    unsigned sd_handle;
    unsigned wd_handle;
    dev_t sd0;
    dev_t wd0;

    fake_init(&sd_media, 0x11u);
    fake_init(&wd_media, 0x55u);
    diskattach(0);
    CHECK(fake_attach_class(&sd_media, DISK_CLASS_SD, &sd_handle) == 0);
    CHECK(fake_attach_class(&wd_media, DISK_CLASS_WD, &wd_handle) == 0);

    /* Each class starts at unit zero even when both backends are present. */
    sd0 = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    wd0 = makedev(3, DISK_MINOR(0, DISK_MINOR_WHOLE));
    CHECK(disk_bdev_open(sd0, FREAD, 0) == 0);
    CHECK(disk_wd_bdev_open(wd0, FREAD, 0) == 0);
    CHECK(disk_bdev_open(makedev(2, DISK_MINOR(1, 0)), FREAD, 0) == ENXIO);
    CHECK(disk_wd_bdev_open(makedev(3, DISK_MINOR(1, 0)), FREAD, 0) ==
        ENXIO);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = sd0;
    bp.b_bcount = sizeof(sd_data);
    bp.b_addr = (caddr_t)sd_data;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(sd_data[0] == sd_media.data[0]);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = wd0;
    bp.b_bcount = sizeof(wd_data);
    bp.b_addr = (caddr_t)wd_data;
    bp.b_flags = B_READ;
    disk_wd_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(wd_data[0] == wd_media.data[0]);
    CHECK(disk_bdev_close(sd0, FREAD, 0) == 0);
    CHECK(disk_wd_bdev_close(wd0, FREAD, 0) == 0);
    disk_detach(sd_handle, &sd_media);
    disk_detach(wd_handle, &wd_media);
    return 0;
}

static int
test_romdisk(void)
{
    struct romdisk romdisk;
    struct buf bp;
    unsigned char media[2u * TEST_ROMDISK_BLOCK_BYTES];
    unsigned char data[TEST_ROMDISK_BLOCK_BYTES];
    unsigned i;
    dev_t dev;
    int blocks;

    for (i = 0; i < sizeof(media); ++i)
        media[i] = (unsigned char)(i ^ 0x5au);
    test_zero(&romdisk, sizeof(romdisk));
    romdisk.rd_start = media;
    romdisk.rd_end = media + sizeof(media);
    romdisk.rd_minor = 7;
    romdisk.rd_block_shift = 10;
    dev = makedev(0, romdisk.rd_minor);

    CHECK(romdisk_bdev_open(&romdisk, makedev(0, 6), FREAD, 0) == ENXIO);
    CHECK(romdisk_bdev_open(&romdisk, dev, FWRITE, 0) == EROFS);
    CHECK(romdisk_bdev_open(&romdisk, dev, FREAD, 0) == 0);
    CHECK(romdisk_bdev_size(&romdisk, dev) == 2);
    CHECK(romdisk_bdev_size(&romdisk, makedev(0, 6)) == 0);
    blocks = 0;
    CHECK(romdisk_bdev_ioctl(&romdisk, dev, DIOCGETMEDIASIZE,
        (caddr_t)&blocks, FREAD) == 0);
    CHECK(blocks == 2);

    test_zero(&bp, sizeof(bp));
    test_zero(data, sizeof(data));
    bp.b_dev = dev;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    romdisk_bdev_strategy(&romdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    CHECK(test_equal(data, media + TEST_ROMDISK_BLOCK_BYTES, sizeof(data)));

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    romdisk_bdev_strategy(&romdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == sizeof(data));

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_PHYS;
    romdisk_bdev_strategy(&romdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == (B_DONE | B_ERROR));
    CHECK(bp.b_error == EROFS);
    CHECK(romdisk_bdev_close(&romdisk, dev, FREAD, 0) == 0);
    return 0;
}

static int
test_ramdisk(void)
{
    struct ramdisk ramdisk;
    struct ramdisk_config config;
    struct buf bp;
    unsigned char media[2u * TEST_ROMDISK_BLOCK_BYTES];
    unsigned char data[TEST_ROMDISK_BLOCK_BYTES];
    unsigned i;
    dev_t dev;
    int blocks;

    test_zero(media, sizeof(media));
    test_zero(&ramdisk, sizeof(ramdisk));
    config.rdc_backing = media;
    config.rdc_backing_bytes = sizeof(media);
    config.rdc_media_bytes = sizeof(media);
    config.rdc_minor = 1;
    config.rdc_block_shift = 10;
    config.rdc_flags = 0;
    config.rdc_compression = 0;
    config.rdc_compression_metadata = 0;
    config.rdc_compression_metadata_bytes = 0;
    CHECK(ramdisk_init(&ramdisk, &config) == 0);
    dev = makedev(1, config.rdc_minor);

    CHECK(ramdisk_bdev_open(&ramdisk, makedev(1, 0), FREAD, 0) == ENXIO);
    CHECK(ramdisk_bdev_open(&ramdisk, dev, FREAD | FWRITE, 0) == 0);
    CHECK(ramdisk_bdev_size(&ramdisk, dev) == 2);
    blocks = 0;
    CHECK(ramdisk_bdev_ioctl(&ramdisk, dev, DIOCGETMEDIASIZE,
        (caddr_t)&blocks, FREAD) == 0);
    CHECK(blocks == 2);

    for (i = 0; i < sizeof(data); ++i)
        data[i] = (unsigned char)(i ^ 0xa5u);
    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_PHYS;
    ramdisk_bdev_strategy(&ramdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    CHECK(test_equal(media + TEST_ROMDISK_BLOCK_BYTES, data,
        sizeof(data)));

    test_zero(data, sizeof(data));
    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    ramdisk_bdev_strategy(&ramdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(test_equal(data, media + TEST_ROMDISK_BLOCK_BYTES,
        sizeof(data)));

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    ramdisk_bdev_strategy(&ramdisk, &bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == sizeof(data));
    CHECK(ramdisk_bdev_close(&ramdisk, dev, FREAD | FWRITE, 0) == 0);
    return 0;
}

static int
test_partition_write_and_flush(void)
{
    struct fake_media media;
    struct buf bp;
    unsigned char data[2 * DISK_SECTOR_SIZE];
    unsigned unit;
    unsigned i;
    dev_t dev;
    dev_t whole;

    fake_init(&media, 7);
    test_zero(media.data, DISK_SECTOR_SIZE);
    media.data[446 + 4] = 0x83;
    set_le32(media.data + 446 + 8, 8);
    set_le32(media.data + 446 + 12, 32);
    media.data[510] = 0x55;
    media.data[511] = 0xaa;
    for (i = 0; i < sizeof(data); ++i)
        data[i] = (unsigned char)(0xc3u ^ i);

    diskattach(0);
    CHECK(fake_writable_attach(&media, &unit) == 0 && unit == 0);
    dev = makedev(2, DISK_MINOR(0, DISK_MINOR_PARTITION(0)));
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    CHECK(disk_bdev_open(dev, FREAD | FWRITE, 0) == 0);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    CHECK(media.write_count == 1);
    CHECK(test_equal(media.data + 10 * DISK_SECTOR_SIZE, data,
        sizeof(data)));

    CHECK(disk_bdev_ioctl(dev, DIOCFLUSH, 0, FWRITE) == 0);
    CHECK(media.flush_count == 1);
    CHECK(disk_bdev_ioctl(dev, DIOCREINIT, 0, FWRITE) == EINVAL);
    CHECK(disk_bdev_open(whole, FREAD, 0) == 0);
    CHECK(disk_bdev_ioctl(whole, DIOCREINIT, 0, FREAD) == EBUSY);
    CHECK(disk_bdev_close(dev, FREAD | FWRITE, 0) == 0);
    CHECK(media.flush_count == 1);
    CHECK(disk_bdev_ioctl(whole, DIOCREINIT, 0, FREAD) == 0);
    CHECK(disk_bdev_close(whole, FREAD, 0) == 0);
    disk_detach(unit, &media);
    return 0;
}

static int
test_raw_odd_sector_addressing(void)
{
    struct fake_media media;
    struct buf bp;
    unsigned char data[DISK_SECTOR_SIZE];
    unsigned unit;
    unsigned i;
    dev_t whole;

    fake_init(&media, 11);
    for (i = 0; i < DISK_SECTOR_SIZE; ++i) {
        media.data[DISK_SECTOR_SIZE + i] = 0xa5;
        media.data[2 * DISK_SECTOR_SIZE + i] = 0x5a;
    }
    diskattach(0);
    CHECK(fake_writable_attach(&media, &unit) == 0 && unit == 0);
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));

    test_zero(&bp, sizeof(bp));
    test_zero(data, sizeof(data));
    bp.b_dev = whole;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    for (i = 0; i < sizeof(data); ++i)
        CHECK(data[i] == 0xa5);
    CHECK(media.phys_read_count == 1);

    for (i = 0; i < sizeof(data); ++i)
        data[i] = 0x3c;
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 3;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    CHECK(test_equal(media.data + 3 * DISK_SECTOR_SIZE, data,
        sizeof(data)));
    CHECK(media.phys_write_count == 1);

    for (i = 0; i < sizeof(data); ++i)
        data[i] = 0x69;
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = TEST_SECTORS - 1u;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(bp.b_resid == 0);
    CHECK(test_equal(media.data + (TEST_SECTORS - 1u) * DISK_SECTOR_SIZE,
        data, sizeof(data)));
    CHECK(media.phys_write_count == 2);

    disk_detach(unit, &media);
    return 0;
}

static int
test_buffered_read_ahead(void)
{
    struct fake_media media;
    struct buf bp;
    unsigned char first[2 * DISK_SECTOR_SIZE];
    unsigned char second[2 * DISK_SECTOR_SIZE];
    unsigned char update[DISK_SECTOR_SIZE];
    unsigned unit;
    unsigned i;
    dev_t whole;

    fake_init(&media, 19);
    diskattach(0);
    CHECK(fake_writable_attach(&media, &unit) == 0 && unit == 0);
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    media.read_count = 0;

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 0;
    bp.b_bcount = sizeof(first);
    bp.b_addr = (caddr_t)first;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.read_count == 1 && media.last_read_sector == 0);
    CHECK(media.last_read_count == 16);
    CHECK(test_equal(first, media.data, sizeof(first)));

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(second);
    bp.b_addr = (caddr_t)second;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.read_count == 1);
    CHECK(test_equal(second, media.data + 2 * DISK_SECTOR_SIZE,
        sizeof(second)));

    for (i = 0; i < sizeof(update); ++i)
        update[i] = 0xd6;
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(update);
    bp.b_addr = (caddr_t)update;
    bp.b_flags = B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(second);
    bp.b_addr = (caddr_t)second;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.read_count == 2 && media.last_read_count == 16);
    CHECK(test_equal(second, update, sizeof(update)));

    disk_detach(unit, &media);
    return 0;
}

static int
test_buffered_write_combining(void)
{
    struct fake_media media;
    struct buf bp;
    unsigned char first[2 * DISK_SECTOR_SIZE];
    unsigned char second[2 * DISK_SECTOR_SIZE];
    unsigned char observed[4 * DISK_SECTOR_SIZE];
    unsigned char window[16 * DISK_SECTOR_SIZE];
    unsigned char expected[16 * DISK_SECTOR_SIZE];
    unsigned char raw[DISK_SECTOR_SIZE];
    unsigned unit;
    unsigned i;
    dev_t whole;

    fake_init(&media, 23);
    for (i = 0; i < sizeof(first); ++i) {
        first[i] = (unsigned char)(0xa0u ^ i);
        second[i] = (unsigned char)(0x5au ^ i);
    }
    for (i = 0; i < sizeof(raw); ++i)
        raw[i] = 0x3cu;

    diskattach(0);
    CHECK(fake_write_back_attach(&media, &unit) == 0 && unit == 0);
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    CHECK(disk_bdev_open(whole, FREAD | FWRITE, 0) == 0);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 1;
    bp.b_bcount = sizeof(first);
    bp.b_addr = (caddr_t)first;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(second);
    bp.b_addr = (caddr_t)second;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.write_count == 0);

    /* A direct full read-ahead-sized read must overlay dirty sectors too. */
    test_copy(expected, media.data, sizeof(expected));
    test_copy(expected + 2 * DISK_SECTOR_SIZE, first, sizeof(first));
    test_copy(expected + 4 * DISK_SECTOR_SIZE, second, sizeof(second));
    test_zero(window, sizeof(window));
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 0;
    bp.b_bcount = sizeof(window);
    bp.b_addr = (caddr_t)window;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(test_equal(window, expected, sizeof(window)));

    /* Reads must see accepted writes before the combined write is flushed. */
    test_zero(observed, sizeof(observed));
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(observed);
    bp.b_addr = (caddr_t)observed;
    bp.b_flags = B_READ | B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(test_equal(observed, first, sizeof(first)));
    CHECK(test_equal(observed + sizeof(first), second, sizeof(second)));

    /* A failed flush retains dirty data for a later retry. */
    media.write_error = EIO;
    CHECK(disk_bdev_ioctl(whole, DIOCFLUSH, 0, FWRITE) == EIO);
    CHECK(media.write_count == 0 && media.flush_count == 0);
    media.write_error = 0;
    CHECK(disk_bdev_ioctl(whole, DIOCFLUSH, 0, FWRITE) == 0);
    CHECK(media.write_count == 1 && media.flush_count == 1);
    CHECK(media.last_write_sector == 2 && media.last_write_count == 4);
    CHECK(test_equal(media.data + 2 * DISK_SECTOR_SIZE, first,
        sizeof(first)));
    CHECK(test_equal(media.data + 4 * DISK_SECTOR_SIZE, second,
        sizeof(second)));

    /* A raw write drains older buffered writes without an extra barrier. */
    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 10;
    bp.b_bcount = sizeof(first);
    bp.b_addr = (caddr_t)first;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.write_count == 1);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = whole;
    bp.b_blkno = 30;
    bp.b_bcount = sizeof(raw);
    bp.b_addr = (caddr_t)raw;
    bp.b_flags = B_PHYS;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.write_count == 3 && media.flush_count == 1);
    CHECK(test_equal(media.data + 20 * DISK_SECTOR_SIZE, first,
        sizeof(first)));
    CHECK(test_equal(media.data + 30 * DISK_SECTOR_SIZE, raw,
        sizeof(raw)));

    CHECK(disk_bdev_close(whole, FREAD | FWRITE, 0) == 0);
    CHECK(media.flush_count == 2);
    disk_detach(unit, &media);
    return 0;
}

static int
test_write_and_flush_errors(void)
{
    struct fake_media media;
    struct buf bp;
    unsigned char data[DISK_SECTOR_SIZE];
    unsigned unit;
    dev_t dev;

    fake_init(&media, 9);
    diskattach(0);
    CHECK(fake_writable_attach(&media, &unit) == 0 && unit == 0);
    dev = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    CHECK(disk_bdev_open(dev, FWRITE, 0) == 0);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    media.write_error = EIO;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == (B_DONE | B_ERROR));
    CHECK(bp.b_error == EIO && bp.b_resid == sizeof(data));
    CHECK(media.flush_count == 0);

    media.write_error = 0;
    test_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 2;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    media.flush_error = EIO;
    CHECK(disk_bdev_ioctl(dev, DIOCREINIT, 0, FWRITE) == EIO);
    CHECK(media.flush_count == 1);
    media.flush_error = 0;
    CHECK(disk_bdev_ioctl(dev, DIOCREINIT, 0, FWRITE) == 0);
    CHECK(media.flush_count == 2);
    CHECK(disk_bdev_close(dev, FWRITE, 0) == 0);
    CHECK(media.flush_count == 2);
    disk_detach(unit, &media);
    return 0;
}

static int
test_gpt_64_bit_lifecycle(void)
{
    struct fake_gpt_media media;
    struct diskpart64 part64;
    struct diskpart legacy;
    struct buf bp;
    unsigned char data[DISK_SECTOR_SIZE];
    disk_sector_t sectors;
    unsigned scheme;
    unsigned old_sectors;
    unsigned unit;
    dev_t whole;
    dev_t part;

    fake_gpt_init(&media);
    diskattach(0);
    CHECK(fake_gpt_attach(&media, &unit) == 0 && unit == 0);
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    part = makedev(2, DISK_MINOR(0, DISK_MINOR_PARTITION(0)));
    CHECK(disk_bdev_open(whole, FREAD, 0) == 0);
    CHECK(disk_bdev_ioctl(whole, DIOCGETSECTORS64, (caddr_t)&sectors,
        FREAD) == 0 && sectors == TEST_GPT_MEDIA_SECTORS);
    CHECK(disk_bdev_ioctl(whole, DIOCGETSECTORS, (caddr_t)&old_sectors,
        FREAD) == EOVERFLOW);
    CHECK(disk_bdev_ioctl(whole, DIOCGETSCHEME, (caddr_t)&scheme,
        FREAD) == 0 && scheme == DISK_SCHEME_GPT);
    CHECK(disk_bdev_open(part, FREAD, 0) == 0);
    CHECK(disk_bdev_ioctl(part, DIOCGETPART64, (caddr_t)&part64,
        FREAD) == 0);
    CHECK(part64.dp_scheme == DISK_SCHEME_GPT);
    CHECK(part64.dp_offset == TEST_GPT_PARTITION_LBA);
    CHECK(part64.dp_nsectors == 100u);
    CHECK(disk_bdev_ioctl(part, DIOCGETPART, (caddr_t)&legacy,
        FREAD) == EOPNOTSUPP);

    test_zero(&bp, sizeof(bp));
    bp.b_dev = part;
    bp.b_blkno = 0;
    bp.b_bcount = sizeof(data);
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ;
    disk_bdev_strategy(&bp);
    CHECK((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE);
    CHECK(media.last_read_lba == TEST_GPT_PARTITION_LBA);
    CHECK(disk_bdev_close(part, FREAD, 0) == 0);
    CHECK(disk_bdev_close(whole, FREAD, 0) == 0);
    disk_detach(unit, &media);

    /* A bad primary header must fall back to the valid backup table. */
    fake_gpt_init(&media);
    media.primary[16] ^= 1u;
    diskattach(0);
    CHECK(fake_gpt_attach(&media, &unit) == 0 && unit == 0);
    part = makedev(2, DISK_MINOR(0, DISK_MINOR_PARTITION(0)));
    CHECK(disk_bdev_open(part, FREAD, 0) == 0);
    CHECK(disk_bdev_close(part, FREAD, 0) == 0);
    disk_detach(unit, &media);

    /* If both entry arrays fail CRC, only the whole disk is exposed. */
    fake_gpt_init(&media);
    media.entries[127] ^= 1u;
    diskattach(0);
    CHECK(fake_gpt_attach(&media, &unit) == 0 && unit == 0);
    whole = makedev(2, DISK_MINOR(0, DISK_MINOR_WHOLE));
    part = makedev(2, DISK_MINOR(0, DISK_MINOR_PARTITION(0)));
    CHECK(disk_bdev_open(whole, FREAD, 0) == 0);
    CHECK(disk_bdev_open(part, FREAD, 0) == ENXIO);
    CHECK(disk_bdev_close(whole, FREAD, 0) == 0);
    disk_detach(unit, &media);
    return 0;
}

int
main(void)
{
    CHECK(test_romdisk() == 0);
    CHECK(test_ramdisk() == 0);
    CHECK(test_open_detach_reuse() == 0);
    CHECK(test_disk_class_namespaces() == 0);
    CHECK(test_partition_write_and_flush() == 0);
    CHECK(test_raw_odd_sector_addressing() == 0);
    CHECK(test_buffered_read_ahead() == 0);
    CHECK(test_buffered_write_combining() == 0);
    CHECK(test_write_and_flush_errors() == 0);
    CHECK(test_gpt_64_bit_lifecycle() == 0);
    puts("disk_lifecycle_test: all tests passed");
    return 0;
}
