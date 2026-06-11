#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <machine/n64cart_uart.h>
#include <machine/ramswap.h>
#include <machine/romdisk.h>

extern struct tty cnttys[];

int
nulldev(void)
{
    return 0;
}

int
noopen(dev_t dev, int flag, int mode)
{
    return ENXIO;
}

int
norw(dev_t dev, struct uio *uio, int flag)
{
    return EIO;
}

int
noioctl(dev_t dev, u_int cmd, caddr_t data, int flag)
{
    return EIO;
}

daddr_t
nosize(dev_t dev)
{
    return 0;
}

static void
n64_nostrategy(struct buf *bp)
{
}

static int
n64_seltrue(dev_t dev, int rw)
{
    return 1;
}

static int
n64_nullstop(struct tty *tp, int flag)
{
    return 0;
}

static char
n64_console_raw_read(dev_t dev)
{
    return n64cart_uart_getc();
}

static void
n64_console_raw_write(dev_t dev, char ch)
{
    n64cart_uart_putc(ch);
}

#define NOBDEV \
    noopen, noopen, n64_nostrategy, nosize, noioctl, 0

const struct bdevsw bdevsw[] = {
    {
        n64romdisk_open, n64romdisk_close, n64romdisk_strategy,
        n64romdisk_size, n64romdisk_ioctl, 0,
    },
    {
        n64ramswap_open, n64ramswap_close, n64ramswap_strategy,
        n64ramswap_size, n64ramswap_ioctl, 0,
    },
    { 0 },
};

const int nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]) - 1;

#define NOCDEV \
    noopen, noopen, norw, norw, noioctl, n64_nullstop, 0, n64_seltrue, \
    n64_nostrategy, 0, 0

const struct cdevsw cdevsw[] = {
    {
        cnopen, cnclose, cnread, cnwrite,
        cnioctl, n64_nullstop, cnttys, cnselect,
        n64_nostrategy, n64_console_raw_read, n64_console_raw_write,
    },
    { NOCDEV },
    { 0 },
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

int
iskmemdev(dev_t dev)
{
    return 0;
}

int
isdisk(dev_t dev, int type)
{
    if (type != IFBLK)
        return 0;

    return major(dev) == N64_ROMDISK_MAJOR ||
        major(dev) == N64_RAMSWAP_MAJOR;
}

int
chrtoblk(dev_t dev)
{
    return NODEV;
}
