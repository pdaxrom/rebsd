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
        DISK_MINOR(unit, DISK_MINOR_WHOLE));
    error = disk_bdev_open(dev, FREAD, 0);
    if (error != 0) {
        i386_early_puts("disk-open: failed\n");
        return error;
    }
    if (disk_bdev_open(dev, FWRITE, 0) != EROFS) {
        i386_early_puts("disk-write-open: failed\n");
        return EIO;
    }
    i386_early_puts("disk-write-open: erofs\n");

    if (disk_bdev_ioctl(dev, DIOCGETSECTORS64, (caddr_t)&sectors,
        FREAD) != 0) {
        i386_early_puts("disk-size: failed\n");
        return EIO;
    }
    i386_early_puts("disk-device: whole\n");

    i386_disk_read(&bp, dev, 0, sectors >= 2u ? 2u : 1u);
    if ((bp.b_flags & (B_DONE | B_ERROR)) != B_DONE ||
        bp.b_resid != 0) {
        i386_early_puts("disk-strategy-read: failed\n");
        return EIO;
    }
    i386_early_puts("disk-strategy-read: ok\n");

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
