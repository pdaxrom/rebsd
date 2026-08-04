#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <disk/romdisk.h>
#include <machine/console.h>
#include <machine/devmajors.h>
#include <machine/ramswap.h>
#include <machine/romdisk.h>
#include <mips/common/devsw.h>
#ifdef VIDEO_ENABLED
#include <sys/drm.h>
#endif
#ifdef INPUT_ENABLED
#include <machine/joybus.h>
#endif

#ifdef N64CART_ENABLED
#include <machine/n64cart_uart.h>
#ifndef N64_CART_UART_ONLY
#include <machine/n64cart_rgbled.h>
#include <machine/n64cart_flash.h>
#endif
#endif

#ifdef PTY_ENABLED
#include <sys/pty.h>
#endif

extern struct tty cnttys[];

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

#define NOBDEV \
    noopen, noopen, nostrategy, nosize, noioctl, 0

const struct bdevsw bdevsw[] = {
    {
        romdisk_open, romdisk_close, romdisk_strategy,
        romdisk_size, romdisk_ioctl, 0,
    },
    {
        n64ramswap_open, n64ramswap_close, n64ramswap_strategy,
        n64ramswap_size, n64ramswap_ioctl,
#ifdef ZSWAP_ENABLED
        BDEV_DISCARD,
#else
        0,
#endif
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
        nostrategy, n64_console_raw_read, n64_console_raw_write,
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
#if N64_TTY_MAJOR != 2
#   error Wrong N64_TTY_MAJOR value!
#endif
        syopen, nullopen, syread, sywrite,
        syioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
    },
    {
#if N64_SERIAL_MAJOR != 3
#   error Wrong N64_SERIAL_MAJOR value!
#endif
#ifdef N64CART_ENABLED
        n64cart_uart_open, n64cart_uart_close,
        n64cart_uart_read, n64cart_uart_write,
        n64cart_uart_ioctl, nullstop,
        n64cart_uart_ttys, n64cart_uart_select,
        nostrategy, n64cart_uart_raw_read, n64cart_uart_raw_write,
#else
        NOCDEV
#endif
    },
    {
#if N64_RGBLED_MAJOR != 4
#   error Wrong N64_RGBLED_MAJOR value!
#endif
#if defined(N64CART_ENABLED) && !defined(N64_CART_UART_ONLY)
        n64cart_rgbled_open, n64cart_rgbled_close, norw, norw,
        n64cart_rgbled_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_FB_MAJOR != 5
#   error Wrong N64_FB_MAJOR value!
#endif
#ifdef VIDEO_ENABLED
        drmfb_open, drmfb_close, drmfb_read, drmfb_write,
        drmfb_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0, drmfb_mmap,
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
        n64joypad_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
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
        n64mouse_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
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
        ptyioctl, nullstop, pt_tty, ptcselect,
        nostrategy, 0, 0,
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
        ptyioctl, nullstop, pt_tty, ptcselect,
        nostrategy, 0, 0,
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
        n64keyboard_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
#else
        NOCDEV
#endif
    },
    {
#if N64_CARTFLASH_MAJOR != 11
#   error Wrong N64_CARTFLASH_MAJOR value!
#endif
#if defined(N64CART_ENABLED) && !defined(N64_CART_UART_ONLY)
        n64cart_flash_open, n64cart_flash_close, norw, norw,
        n64cart_flash_ioctl, nullstop, 0, seltrue,
        nostrategy, 0, 0,
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
