/* Host-side lifecycle tests for removable common block disks. */

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <disk/disk.h>

#define CHECK(expr) do {                                                \
    if (!(expr))                                                        \
        return __LINE__;                                                \
} while (0)

#define TEST_SECTORS               64u

struct fake_media {
    unsigned char data[TEST_SECTORS * DISK_SECTOR_SIZE];
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
biodone(struct buf *bp)
{
    bp->b_flags |= B_DONE;
}

static int
fake_read(void *arg, unsigned sector, unsigned count, void *data)
{
    struct fake_media *media;

    media = (struct fake_media *)arg;
    if (!media->present || count > TEST_SECTORS ||
        sector > TEST_SECTORS - count)
        return EIO;
    test_copy(data, media->data + sector * DISK_SECTOR_SIZE,
        count * DISK_SECTOR_SIZE);
    return 0;
}

static int
fake_present(void *arg)
{
    return ((struct fake_media *)arg)->present;
}

static const struct disk_backend_ops fake_ops = {
    fake_read,
    0,
    0,
    fake_present
};

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
fake_attach(struct fake_media *media, unsigned *unitp)
{
    struct disk_attach_args args;

    test_zero(&args, sizeof(args));
    args.da_ops = &fake_ops;
    args.da_arg = media;
    args.da_sector_count = TEST_SECTORS;
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY | DISK_FLAG_REMOVABLE;
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

int
main(void)
{
    CHECK(test_open_detach_reuse() == 0);
    puts("disk_lifecycle_test: all tests passed");
    return 0;
}
