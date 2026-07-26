#include <sys/param.h>
#include <sys/buf.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>

#include <disk/disk.h>

#include "romdisk.h"

#define I386_DISK_MAJOR 2

static int
i386_noopen(dev_t dev, int flag, int mode)
{
    (void)dev;
    (void)flag;
    (void)mode;
    return ENXIO;
}

static void
i386_nostrategy(struct buf *bp)
{
    bp->b_error = ENXIO;
    bp->b_resid = bp->b_bcount;
    bp->b_flags |= B_ERROR | B_DONE;
}

static daddr_t
i386_nosize(dev_t dev)
{
    (void)dev;
    return 0;
}

static int
i386_noioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    (void)dev;
    (void)cmd;
    (void)data;
    (void)flag;
    return ENXIO;
}

static int
i386_nullstop(struct tty *tp, int flag)
{
    (void)tp;
    (void)flag;
    return 0;
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

#define I386_NOBDEV \
    { i386_noopen, i386_noopen, i386_nostrategy, i386_nosize, \
      i386_noioctl, 0 }

const struct bdevsw bdevsw[] = {
    {
#if I386_ROMDISK_MAJOR != 0
#error Wrong I386_ROMDISK_MAJOR value
#endif
        i386romdisk_open, i386romdisk_close, i386romdisk_strategy,
        i386romdisk_size, i386romdisk_ioctl, 0
    },
    I386_NOBDEV,
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
        cnioctl, i386_nullstop, cnttys, cnselect,
        i386_nostrategy, i386_console_raw_read,
        i386_console_raw_write, 0
    },
    { 0 }
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

dev_t
chrtoblk(dev_t dev)
{
    (void)dev;
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
    return type == IFBLK &&
        (major(dev) == I386_ROMDISK_MAJOR ||
        major(dev) == I386_DISK_MAJOR);
}
