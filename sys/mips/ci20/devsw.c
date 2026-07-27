#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <disk/disk.h>
#include <disk/romdisk.h>
#include <machine/devmajors.h>
#include <machine/ramswap.h>
#include <machine/romdisk.h>
#include <mips/common/devsw.h>
#ifdef VIDEO_ENABLED
#include <sys/drm.h>
#endif

#ifdef PTY_ENABLED
#include <sys/pty.h>
#endif

extern struct tty cnttys[];
extern struct tty ci20_uart_ttys[];

int ci20_uart_open(dev_t dev, int flag, int mode);
int ci20_uart_close(dev_t dev, int flag, int mode);
int ci20_uart_read(dev_t dev, struct uio *uio, int flag);
int ci20_uart_write(dev_t dev, struct uio *uio, int flag);
int ci20_uart_ioctl(dev_t dev, u_int cmd, caddr_t data, int flag);
int ci20_uart_select(dev_t dev, int rw);
char ci20_uart_raw_read(dev_t dev);
void ci20_uart_raw_write(dev_t dev, char ch);

static char
mips_console_raw_read(dev_t dev)
{
    (void)dev;
    return cngetc();
}

static void
mips_console_raw_write(dev_t dev, char ch)
{
    (void)dev;
    cnputc(ch);
}

#define NOBDEV \
    noopen, noopen, nostrategy, nosize, noioctl, 0

const struct bdevsw bdevsw[] = {
    {
        romdisk_open, romdisk_close, romdisk_strategy,
        romdisk_size, romdisk_ioctl, 0,
    },
    {
        mipsramswap_open, mipsramswap_close, mipsramswap_strategy,
        mipsramswap_size, mipsramswap_ioctl, 0,
    },
    {
#if MIPS_DISK_MAJOR != 2
#   error Wrong MIPS_DISK_MAJOR value!
#endif
        disk_bdev_open, disk_bdev_close, disk_bdev_strategy,
        disk_bdev_size, disk_bdev_ioctl, 0,
    },
    { 0 },
};

const int nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]) - 1;

#define NOCDEV \
    noopen, noopen, norw, norw, noioctl, nullstop, 0, seltrue, \
    nostrategy, 0, 0, 0

const struct cdevsw cdevsw[] = {
    {
        cnopen, cnclose, cnread, cnwrite,
        cnioctl, nullstop, cnttys, cnselect,
        nostrategy, mips_console_raw_read, mips_console_raw_write,
    },
    {
#if MEM_MAJOR != 1
#   error Wrong MEM_MAJOR value!
#endif
        nullopen, nullopen, mips_mmrw, mips_mmrw,
        noioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
    },
    {
#if MIPS_TTY_MAJOR != 2
#   error Wrong MIPS_TTY_MAJOR value!
#endif
        syopen, nullopen, syread, sywrite,
        syioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
    },
    {
#if MIPS_SERIAL_MAJOR != 3
#   error Wrong MIPS_SERIAL_MAJOR value!
#endif
        ci20_uart_open, ci20_uart_close,
        ci20_uart_read, ci20_uart_write,
        ci20_uart_ioctl, nullstop,
        ci20_uart_ttys, ci20_uart_select,
        nostrategy, ci20_uart_raw_read, ci20_uart_raw_write,
    },
    { NOCDEV },
    {
#if MIPS_FB_MAJOR != 5
#   error Wrong MIPS_FB_MAJOR value!
#endif
#ifdef VIDEO_ENABLED
        drmfb_open, drmfb_close, drmfb_read, drmfb_write,
        drmfb_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0, drmfb_mmap,
#else
        NOCDEV
#endif
    },
    { NOCDEV },
    { NOCDEV },
    {
#if MIPS_PTS_MAJOR != 8
#   error Wrong MIPS_PTS_MAJOR value!
#endif
#ifdef PTY_ENABLED
        ptsopen, ptsclose, ptsread, ptswrite,
        ptyioctl, nullstop, pt_tty, ptcselect,
        nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if MIPS_PTC_MAJOR != 9
#   error Wrong MIPS_PTC_MAJOR value!
#endif
#ifdef PTY_ENABLED
        ptcopen, ptcclose, ptcread, ptcwrite,
        ptyioctl, nullstop, pt_tty, ptcselect,
        nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if MIPS_RDISK_MAJOR != 10
#   error Wrong MIPS_RDISK_MAJOR value!
#endif
        disk_cdev_open, disk_cdev_close, disk_cdev_read, disk_cdev_write,
        disk_cdev_ioctl, nullstop, 0, seltrue,
        disk_bdev_strategy, 0, 0,
    },
    { 0 },
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

dev_t
chrtoblk(dev_t dev)
{
    if (major(dev) == MIPS_RDISK_MAJOR)
        return makedev(MIPS_DISK_MAJOR, minor(dev));
    return NODEV;
}

int
iskmemdev(dev_t dev)
{
    return major(dev) == MEM_MAJOR && minor(dev) < 2;
}

int
isdisk(dev_t dev, int type)
{
    if (type == IFCHR)
        return major(dev) == MIPS_RDISK_MAJOR;
    if (type != IFBLK)
        return 0;

    return major(dev) == MIPS_ROMDISK_MAJOR ||
        major(dev) == MIPS_RAMSWAP_MAJOR ||
        major(dev) == MIPS_DISK_MAJOR;
}
