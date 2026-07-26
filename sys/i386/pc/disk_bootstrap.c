#include "boot.h"
#include "disk_bootstrap.h"
#include "ide.h"
#include "vfs_bootstrap.h"

#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>

#define I386_DISK_MAJOR 2u

static unsigned char i386_disk_data[DISK_SECTOR_SIZE * 2u];
static struct disk_memory i386_rootfs_memory;

extern const unsigned char _binary_rootfs_img_start[];
extern const unsigned char _binary_rootfs_img_end[];

static void
i386_disk_zero(void *arg, unsigned length)
{
    unsigned char *data;

    data = (unsigned char *)arg;
    while (length-- != 0)
        *data++ = 0;
}

static int
i386_disk_has_at(const unsigned char *data, unsigned offset,
    const char *wanted)
{
    while (*wanted != '\0') {
        if (offset >= DISK_SECTOR_SIZE ||
            data[offset] != (unsigned char)*wanted)
            return 0;
        ++offset;
        ++wanted;
    }
    return 1;
}

static unsigned
i386_disk_fat_type(const unsigned char *data)
{
    if (data[510] != 0x55u || data[511] != 0xaau ||
        !i386_disk_has_at(data, 3, "REBSD   "))
        return 0;
    if (i386_disk_has_at(data, 54, "FAT16   "))
        return 16;
    if (i386_disk_has_at(data, 82, "FAT32   "))
        return 32;
    return 0;
}

static void
i386_disk_read(struct buf *bp, dev_t dev, disk_sector_t lba,
    unsigned count)
{
    i386_disk_zero(bp, sizeof(*bp));
    i386_disk_zero(i386_disk_data, count * DISK_SECTOR_SIZE);
    bp->b_dev = dev;
    bp->b_blkno = (blkno_t)lba;
    bp->b_bcount = count * DISK_SECTOR_SIZE;
    bp->b_addr = (caddr_t)i386_disk_data;
    bp->b_flags = B_READ | B_PHYS;
    disk_bdev_strategy(bp);
}

static int
i386_disk_attach_ide(dev_t *devp)
{
    struct disk_attach_args args;
    struct diskpart64 part;
    struct buf bp;
    disk_sector_t sectors;
    dev_t dev;
    unsigned unit;
    int error;

    i386_disk_zero(&args, sizeof(args));
    args.da_ops = i386_ide_backend_ops();
    args.da_sector_count = i386_ide_sector_count();
    args.da_sector_size = DISK_SECTOR_SIZE;
    args.da_flags = DISK_FLAG_READ_ONLY;

    *devp = NODEV;
    error = disk_attach(&args, &unit);
    if (error != 0) {
        i386_early_puts("disk-attach: failed\n");
        return error;
    }
    i386_early_puts("disk-attach: read-only\n");

    dev = makedev(I386_DISK_MAJOR,
        DISK_MINOR(unit, DISK_MINOR_PARTITION(0)));
    error = disk_bdev_open(dev, FREAD, 0);
    if (error == ENXIO) {
        i386_early_puts("disk-partition: absent\n");
        return 0;
    }
    if (error != 0) {
        i386_early_puts("disk-partition: failed\n");
        return error;
    }
    if (disk_bdev_open(dev, FWRITE, 0) != EROFS) {
        i386_early_puts("disk-write-open: failed\n");
        return EIO;
    }
    i386_early_puts("disk-write-open: erofs\n");

    i386_disk_zero(&part, sizeof(part));
    if (disk_bdev_ioctl(dev, DIOCGETPART64, (caddr_t)&part, FREAD) != 0 ||
        disk_bdev_ioctl(dev, DIOCGETSECTORS64, (caddr_t)&sectors,
        FREAD) != 0) {
        i386_early_puts("disk-partition: failed\n");
        return EIO;
    }
    i386_early_puts("disk-partition: ");
    if (part.dp_scheme == DISK_SCHEME_MBR && part.dp_type == 0x06u)
        i386_early_puts("fat16\n");
    else if (part.dp_scheme == DISK_SCHEME_MBR &&
        (part.dp_type == 0x0bu || part.dp_type == 0x0cu))
        i386_early_puts("fat32\n");
    else
        i386_early_puts("external\n");

    i386_disk_read(&bp, dev, 0, sectors >= 2u ? 2u : 1u);
    if ((bp.b_flags & (B_DONE | B_ERROR)) != B_DONE ||
        bp.b_resid != 0) {
        i386_early_puts("disk-strategy-read: failed\n");
        return EIO;
    }
    if (i386_disk_fat_type(i386_disk_data) == 16)
        i386_early_puts("disk-strategy-read: rebsd-fat16\n");
    else if (i386_disk_fat_type(i386_disk_data) == 32)
        i386_early_puts("disk-strategy-read: rebsd-fat32\n");
    else
        i386_early_puts("disk-strategy-read: external\n");

    i386_disk_read(&bp, dev, sectors, 1);
    if ((bp.b_flags & (B_DONE | B_ERROR)) == B_DONE &&
        bp.b_resid == DISK_SECTOR_SIZE)
        i386_early_puts("disk-strategy-eof: ok\n");
    else {
        i386_early_puts("disk-strategy-eof: failed\n");
        return EIO;
    }

    i386_disk_zero(&bp, sizeof(bp));
    bp.b_dev = dev;
    bp.b_blkno = 0;
    bp.b_bcount = DISK_SECTOR_SIZE;
    bp.b_addr = (caddr_t)i386_disk_data;
    bp.b_flags = B_PHYS;
    disk_bdev_strategy(&bp);
    if ((bp.b_flags & (B_DONE | B_ERROR)) == (B_DONE | B_ERROR) &&
        bp.b_error == EROFS && bp.b_resid == DISK_SECTOR_SIZE)
        i386_early_puts("disk-strategy-write: erofs\n");
    else {
        i386_early_puts("disk-strategy-write: failed\n");
        return EIO;
    }

    if (disk_bdev_close(dev, FREAD, 0) != 0) {
        i386_early_puts("disk-close: failed\n");
        return EIO;
    }
    i386_early_puts("disk-close: ok\n");
    *devp = dev;
    return 0;
}

static int
i386_disk_attach_rootfs(dev_t *devp)
{
    size_t image_size;
    unsigned unit;
    int error;

    *devp = NODEV;
    image_size = (size_t)(_binary_rootfs_img_end -
        _binary_rootfs_img_start);
    error = disk_memory_attach(&i386_rootfs_memory,
        _binary_rootfs_img_start, image_size, &unit);
    if (error != 0) {
        i386_early_puts("rootfs-disk: failed\n");
        return error;
    }
    *devp = makedev(I386_DISK_MAJOR,
        DISK_MINOR(unit, DISK_MINOR_WHOLE));
    if (disk_bdev_open(*devp, FWRITE, 0) != EROFS) {
        i386_early_puts("rootfs-disk: writable\n");
        return EIO;
    }
    i386_early_puts("rootfs-disk: read-only\n");
    return 0;
}

int
i386_disk_bootstrap(int ide_present)
{
    dev_t preferred_dev;
    dev_t fallback_dev;
    int error;

    diskattach(0);
    preferred_dev = NODEV;
    if (ide_present) {
        error = i386_disk_attach_ide(&preferred_dev);
        if (error != 0)
            return error;
    }
    error = i386_disk_attach_rootfs(&fallback_dev);
    if (error != 0)
        return error;
    error = i386_vfs_bootstrap_mount(preferred_dev, fallback_dev);
    if (error != 0)
        return error;
    return 0;
}
