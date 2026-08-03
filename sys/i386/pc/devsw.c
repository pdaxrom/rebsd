#include <sys/param.h>
#include <sys/buf.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/memdev.h>
#include <sys/systm.h>

#include <disk/disk.h>
#include <disk/romdisk.h>
#include <input/mousevar.h>

#include "romdisk.h"
#include "ramdisk.h"

#define I386_DISK_MAJOR 2
#define I386_MOUSE_MAJOR 2
#define I386_RDISK_MAJOR 3

static void
i386_nostrategy(struct buf *bp)
{
    bp->b_error = ENXIO;
    bp->b_resid = bp->b_bcount;
    bp->b_flags |= B_ERROR | B_DONE;
}

static char
i386_console_raw_read(dev_t dev)
{
    (void)dev;
    return (char)cngetc();
}

static void
i386_console_raw_write(dev_t dev, char ch)
{
    (void)dev;
    cnputc(ch);
}

const struct bdevsw bdevsw[] = {
    {
#if I386_ROMDISK_MAJOR != 0
#error Wrong I386_ROMDISK_MAJOR value
#endif
        romdisk_open, romdisk_close, romdisk_strategy,
        romdisk_size, romdisk_ioctl, 0
    },
    {
#if I386_RAMDISK_MAJOR != 1
#error Wrong I386_RAMDISK_MAJOR value
#endif
        i386_ramdisk_open, i386_ramdisk_close, i386_ramdisk_strategy,
        i386_ramdisk_size, i386_ramdisk_ioctl, 0
    },
    {
#if I386_DISK_MAJOR != 2
#error Wrong I386_DISK_MAJOR value
#endif
        disk_bdev_open, disk_bdev_close, disk_bdev_strategy,
        disk_bdev_size, disk_bdev_ioctl, 0
    },
    { 0 }
};

const int nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]) - 1;

const struct cdevsw cdevsw[] = {
    {
        cnopen, cnclose, cnread, cnwrite,
        cnioctl, nullstop, cnttys, cnselect,
        i386_nostrategy, i386_console_raw_read,
        i386_console_raw_write, 0
    },
    {
#if MEM_MAJOR != 1
#error Wrong MEM_MAJOR value
#endif
        memdev_nullzero_open, memdev_nullzero_open,
        memdev_nullzero_rw, memdev_nullzero_rw,
        noioctl, nullstop, 0, seltrue,
        i386_nostrategy, 0, 0, 0
    },
    {
#if I386_MOUSE_MAJOR != 2
#error Wrong I386_MOUSE_MAJOR value
#endif
        mouse_open, mouse_close, mouse_read, norw,
        mouse_ioctl, nullstop, 0, mouse_select,
        i386_nostrategy, 0, 0, 0
    },
    {
#if I386_RDISK_MAJOR != 3
#error Wrong I386_RDISK_MAJOR value
#endif
        disk_cdev_open, disk_cdev_close, disk_cdev_read, disk_cdev_write,
        disk_cdev_ioctl, nullstop, 0, seltrue,
        disk_bdev_strategy, 0, 0, 0
    },
    { 0 }
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

dev_t
chrtoblk(dev_t dev)
{
    if (major(dev) == I386_RDISK_MAJOR)
        return makedev(I386_DISK_MAJOR, minor(dev));
    return NODEV;
}

int
iskmemdev(dev_t dev)
{
    (void)dev;
    return 0;
}

int
isdisk(dev_t dev, int type)
{
    if (type == IFCHR)
        return major(dev) == I386_RDISK_MAJOR;
    if (type != IFBLK)
        return 0;
    return major(dev) == I386_ROMDISK_MAJOR ||
        major(dev) == I386_RAMDISK_MAJOR ||
        major(dev) == I386_DISK_MAJOR;
}
