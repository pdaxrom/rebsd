#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <machine/console.h>
#include <machine/devmajors.h>
#include <machine/ramswap.h>
#include <machine/romdisk.h>
#ifdef VIDEO_ENABLED
#include <machine/video.h>
#endif
#ifdef INPUT_ENABLED
#include <machine/joybus.h>
#endif

#ifdef N64CART_ENABLED
#include <machine/n64cart_uart.h>
#include <machine/n64cart_rgbled.h>
#include <machine/n64cart_flash.h>
#endif

#ifdef PTY_ENABLED
#include <sys/pty.h>
#endif

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

static int
n64_null_open(dev_t dev, int flag, int mode)
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
    return n64_console_getc();
}

static void
n64_console_raw_write(dev_t dev, char ch)
{
    n64_console_putc(ch);
}

static int
n64_mmrw(dev_t dev, struct uio *uio, int flag)
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
                panic("n64_mmrw");
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
    {
#if MEM_MAJOR != 1
#   error Wrong MEM_MAJOR value!
#endif
        n64_null_open, n64_null_open, n64_mmrw, n64_mmrw,
        noioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
    },
    {
#if N64_TTY_MAJOR != 2
#   error Wrong N64_TTY_MAJOR value!
#endif
        syopen, n64_null_open, syread, sywrite,
        syioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
    },
    {
#if N64_SERIAL_MAJOR != 3
#   error Wrong N64_SERIAL_MAJOR value!
#endif
#ifdef N64CART_ENABLED
        n64cart_uart_open, n64cart_uart_close,
        n64cart_uart_read, n64cart_uart_write,
        n64cart_uart_ioctl, n64_nullstop,
        n64cart_uart_ttys, n64cart_uart_select,
        n64_nostrategy, n64cart_uart_raw_read, n64cart_uart_raw_write,
#else
        NOCDEV
#endif
    },
    {
#if N64_RGBLED_MAJOR != 4
#   error Wrong N64_RGBLED_MAJOR value!
#endif
#ifdef N64CART_ENABLED
        n64cart_rgbled_open, n64cart_rgbled_close, norw, norw,
        n64cart_rgbled_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_FB_MAJOR != 5
#   error Wrong N64_FB_MAJOR value!
#endif
#ifdef VIDEO_ENABLED
        n64fb_open, n64fb_close, n64fb_read, n64fb_write,
        n64fb_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_JOYPAD_MAJOR != 6
#   error Wrong N64_JOYPAD_MAJOR value!
#endif
#ifdef INPUT_ENABLED
        n64joypad_open, n64joypad_close, n64joypad_read, norw,
        n64joypad_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_MOUSE_MAJOR != 7
#   error Wrong N64_MOUSE_MAJOR value!
#endif
#ifdef INPUT_ENABLED
        n64mouse_open, n64mouse_close, n64mouse_read, norw,
        n64mouse_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_PTS_MAJOR != 8
#   error Wrong N64_PTS_MAJOR value!
#endif
#ifdef PTY_ENABLED
        ptsopen, ptsclose, ptsread, ptswrite,
        ptyioctl, n64_nullstop, pt_tty, ptcselect,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_PTC_MAJOR != 9
#   error Wrong N64_PTC_MAJOR value!
#endif
#ifdef PTY_ENABLED
        ptcopen, ptcclose, ptcread, ptcwrite,
        ptyioctl, n64_nullstop, pt_tty, ptcselect,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_KBD_MAJOR != 10
#   error Wrong N64_KBD_MAJOR value!
#endif
#ifdef INPUT_ENABLED
        n64keyboard_open, n64keyboard_close, n64keyboard_read, norw,
        n64keyboard_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_CARTFLASH_MAJOR != 11
#   error Wrong N64_CARTFLASH_MAJOR value!
#endif
#ifdef N64CART_ENABLED
        n64cart_flash_open, n64cart_flash_close, norw, norw,
        n64cart_flash_ioctl, n64_nullstop, 0, n64_seltrue,
        n64_nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    { 0 },
};

const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

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

    return major(dev) == N64_ROMDISK_MAJOR ||
        major(dev) == N64_RAMSWAP_MAJOR;
}

int
chrtoblk(dev_t dev)
{
    return NODEV;
}
