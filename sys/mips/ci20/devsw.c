#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <disk/disk.h>
#include <machine/devmajors.h>
#include <machine/ramswap.h>
#include <machine/romdisk.h>

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

static int
mips_null_open(dev_t dev, int flag, int mode)
{
    return 0;
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
mips_nostrategy(struct buf *bp)
{
}

static int
mips_seltrue(dev_t dev, int rw)
{
    return 1;
}

static int
mips_nullstop(struct tty *tp, int flag)
{
    return 0;
}

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

static int
mips_mmrw(dev_t dev, struct uio *uio, int flag)
{
    register struct iovec *iov;
    int error;
    register u_int c;

    error = 0;
    while (uio->uio_resid && error == 0) {
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            if (uio->uio_iovcnt < 0)
                panic("mips_mmrw");
            continue;
        }

        switch (minor(dev)) {
        case 0:
        case 1:
            if ((badkaddr((caddr_t)uio->uio_offset) &&
                baduaddr((caddr_t)uio->uio_offset)) ||
                (badkaddr((caddr_t)(uio->uio_offset + iov->iov_len - 1)) &&
                baduaddr((caddr_t)(uio->uio_offset + iov->iov_len - 1))))
                return EFAULT;
            error = uiomove((caddr_t)uio->uio_offset, iov->iov_len, uio);
            break;
        case 2:
            if (uio->uio_rw == UIO_READ)
                return 0;
            c = iov->iov_len;
            iov->iov_base += c;
            iov->iov_len -= c;
            uio->uio_offset += c;
            uio->uio_resid -= c;
            break;
        case 3:
            if (uio->uio_rw == UIO_WRITE)
                return EIO;
            c = iov->iov_len;
            bzero(iov->iov_base, c);
            iov->iov_base += c;
            iov->iov_len -= c;
            uio->uio_offset += c;
            uio->uio_resid -= c;
            break;
        default:
            return EINVAL;
        }
    }

    return error;
}

#define NOBDEV \
    noopen, noopen, mips_nostrategy, nosize, noioctl, 0

const struct bdevsw bdevsw[] = {
    {
        mipsromdisk_open, mipsromdisk_close, mipsromdisk_strategy,
        mipsromdisk_size, mipsromdisk_ioctl, 0,
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
    noopen, noopen, norw, norw, noioctl, mips_nullstop, 0, mips_seltrue, \
    mips_nostrategy, 0, 0

const struct cdevsw cdevsw[] = {
    {
        cnopen, cnclose, cnread, cnwrite,
        cnioctl, mips_nullstop, cnttys, cnselect,
        mips_nostrategy, mips_console_raw_read, mips_console_raw_write,
    },
    {
#if MEM_MAJOR != 1
#   error Wrong MEM_MAJOR value!
#endif
        mips_null_open, mips_null_open, mips_mmrw, mips_mmrw,
        noioctl, mips_nullstop, 0, mips_seltrue,
        mips_nostrategy, 0, 0,
    },
    {
#if MIPS_TTY_MAJOR != 2
#   error Wrong MIPS_TTY_MAJOR value!
#endif
        syopen, mips_null_open, syread, sywrite,
        syioctl, mips_nullstop, 0, mips_seltrue,
        mips_nostrategy, 0, 0,
    },
    {
#if MIPS_SERIAL_MAJOR != 3
#   error Wrong MIPS_SERIAL_MAJOR value!
#endif
        ci20_uart_open, ci20_uart_close,
        ci20_uart_read, ci20_uart_write,
        ci20_uart_ioctl, mips_nullstop,
        ci20_uart_ttys, ci20_uart_select,
        mips_nostrategy, ci20_uart_raw_read, ci20_uart_raw_write,
    },
    { NOCDEV },
    { NOCDEV },
    { NOCDEV },
    { NOCDEV },
    {
#if MIPS_PTS_MAJOR != 8
#   error Wrong MIPS_PTS_MAJOR value!
#endif
#ifdef PTY_ENABLED
        ptsopen, ptsclose, ptsread, ptswrite,
        ptyioctl, mips_nullstop, pt_tty, ptcselect,
        mips_nostrategy, 0, 0,
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
        ptyioctl, mips_nullstop, pt_tty, ptcselect,
        mips_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    { 0 },
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

dev_t
chrtoblk(dev_t dev)
{
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
    if (type != IFBLK)
        return 0;

    return major(dev) == MIPS_ROMDISK_MAJOR ||
        major(dev) == MIPS_RAMSWAP_MAJOR;
}
