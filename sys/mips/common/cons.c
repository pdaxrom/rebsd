#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <machine/console.h>

struct tty cnttys[1];
static void cnstart(struct tty *tp);
static void cninput(int c);
void cnintr(void);
void cnputc(char c);

int
cnopen(dev_t dev, int flag, int mode)
{
    struct tty *tp;

    if (minor(dev) != 0)
        return ENXIO;

    tp = &cnttys[0];
    tp->t_oproc = cnstart;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        tp->t_ispeed = B115200;
        tp->t_ospeed = B115200;
        ttychars(tp);
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA |
            CTLECH | CRTKIL;
    }
    mips_console_tty_winsize(tp);
    tp->t_state |= TS_CARR_ON;

    return ttyopen(dev, tp);
}

int
cnclose(dev_t dev, int flag, int mode)
{
    struct tty *tp = &cnttys[0];

    ttywflush(tp);
    ttyclose(tp);
    return 0;
}

int
cnread(dev_t dev, struct uio *uio, int flag)
{
    cnintr();
    return ttread(&cnttys[0], uio, flag);
}

int
cnwrite(dev_t dev, struct uio *uio, int flag)
{
    return ttwrite(&cnttys[0], uio, flag);
}

static void
cninput(int c)
{
    if (c == '\b' || c == '\177')
        c = '\177';
    ttyinput(c, &cnttys[0]);
}

void
cnintr(void)
{
    if ((cnttys[0].t_state & TS_ISOPEN) == 0)
        return;

    while (mips_console_poll()) {
        cninput(mips_console_getc());
    }
}

static void
cnstart(struct tty *tp)
{
    int c;
    int s;

    s = spltty();
    if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
        splx(s);
        return;
    }
    tp->t_state |= TS_BUSY;
    while ((c = getc(&tp->t_outq)) >= 0) {
        splx(s);
        mips_console_putc(c);
        s = spltty();
    }
    tp->t_state &= ~TS_BUSY;
    ttyowake(tp);
    splx(s);
}

int
cnselect(dev_t dev, int rw)
{
    cnintr();
    return ttyselect(&cnttys[0], rw);
}

int
cnioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int error;

    if (cmd == TIOCGWINSZ) {
        mips_console_tty_winsize(&cnttys[0]);
        *(struct winsize *)addr = cnttys[0].t_winsize;
        return 0;
    }
    error = ttioctl(&cnttys[0], cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return error;
}

void
cnputc(char c)
{
    if (c == '\n')
        mips_console_putc('\r');
    mips_console_putc(c);
}

int
cngetc(void)
{
    return mips_console_getc();
}
