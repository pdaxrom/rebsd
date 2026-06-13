#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/kconfig.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/user.h>
#include <machine/console.h>
#include <machine/n64cart_uart.h>

struct tty n64cart_uart_ttys[1];
static void n64cart_uart_start(struct tty *tp);
static int n64cart_uart_esc_state;

static void
n64cart_uart_default_winsize(struct tty *tp)
{
    if (tp->t_winsize.ws_row == 0)
        tp->t_winsize.ws_row = 24;
    if (tp->t_winsize.ws_col == 0)
        tp->t_winsize.ws_col = 80;
}

#define N64CART_UART_ESC_NONE   0
#define N64CART_UART_ESC_ESC    1
#define N64CART_UART_ESC_CSI    2
#define N64CART_UART_ESC_CSI_3  3

static void
n64cart_uart_input_normal(struct tty *tp, int c)
{
    if (c == '\b' || c == '\177')
        c = '\177';
    ttyinput(c, tp);
}

static void
n64cart_uart_input(struct tty *tp, int c)
{
again:
    switch (n64cart_uart_esc_state) {
    case N64CART_UART_ESC_ESC:
        if (c == '[') {
            n64cart_uart_esc_state = N64CART_UART_ESC_CSI;
            return;
        }
        ttyinput('\033', tp);
        n64cart_uart_esc_state = N64CART_UART_ESC_NONE;
        goto again;

    case N64CART_UART_ESC_CSI:
        if (c == '3') {
            n64cart_uart_esc_state = N64CART_UART_ESC_CSI_3;
            return;
        }
        ttyinput('\033', tp);
        ttyinput('[', tp);
        n64cart_uart_esc_state = N64CART_UART_ESC_NONE;
        goto again;

    case N64CART_UART_ESC_CSI_3:
        n64cart_uart_esc_state = N64CART_UART_ESC_NONE;
        if (c == '~') {
            ttyinput('\177', tp);
            return;
        }
        ttyinput('\033', tp);
        ttyinput('[', tp);
        ttyinput('3', tp);
        goto again;

    default:
        break;
    }

    if (c == '\033') {
        n64cart_uart_esc_state = N64CART_UART_ESC_ESC;
        return;
    }
    n64cart_uart_input_normal(tp, c);
}

static int
n64cart_init(void *arg)
{
    (void)arg;
    return 1;
}

struct driver n64cartdriver = {
    "n64cart",
    n64cart_init,
};

static volatile u_int *
n64cart_reg(u_int offset)
{
    return (volatile u_int *)(N64CART_UART_BASE + offset);
}

static u_int
n64cart_read(u_int offset)
{
    u_int value;

    asm volatile ("" ::: "memory");
    value = *n64cart_reg(offset);
    asm volatile ("" ::: "memory");
    return value;
}

static void
n64cart_write(u_int offset, u_int value)
{
    asm volatile ("" ::: "memory");
    *n64cart_reg(offset) = value;
    asm volatile ("" ::: "memory");
}

int
n64cart_uart_poll(void)
{
    return (n64cart_read(N64CART_UART_CTRL) & N64CART_UART_RX_AVAIL) != 0;
}

int
n64cart_uart_getc(void)
{
    while (!n64cart_uart_poll())
        ;
    return n64cart_read(N64CART_UART_RXTX) & 0xff;
}

void
n64cart_uart_putc(int ch)
{
    while ((n64cart_read(N64CART_UART_CTRL) & N64CART_UART_TX_FREE) == 0)
        ;

    n64cart_write(N64CART_UART_RXTX, ch & 0xff);
    (void)n64cart_read(N64CART_UART_CTRL);
}

void
n64cart_led_write(unsigned rgb)
{
    n64cart_write(N64CART_LED_CTRL, rgb & N64CART_LED_RGB_MASK);
}

char
n64cart_uart_raw_read(dev_t dev)
{
    (void)dev;
    return n64cart_uart_getc();
}

void
n64cart_uart_raw_write(dev_t dev, char ch)
{
    (void)dev;
    n64cart_uart_putc(ch);
}

int
n64cart_uart_open(dev_t dev, int flag, int mode)
{
    struct tty *tp;

    if (minor(dev) != 0)
        return ENXIO;

    tp = &n64cart_uart_ttys[0];
    tp->t_oproc = n64cart_uart_start;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        tp->t_ispeed = B115200;
        tp->t_ospeed = B115200;
        ttychars(tp);
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA |
            CTLECH | CRTKIL;
    }
    n64cart_uart_default_winsize(tp);
    tp->t_state |= TS_CARR_ON;
    if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
        return EBUSY;

    return ttyopen(dev, tp);
}

int
n64cart_uart_close(dev_t dev, int flag, int mode)
{
    struct tty *tp = &n64cart_uart_ttys[0];

    if (minor(dev) != 0)
        return ENXIO;
    ttywflush(tp);
    ttyclose(tp);
    return 0;
}

int
n64cart_uart_read(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    n64cart_uart_intr();
    return ttread(&n64cart_uart_ttys[0], uio, flag);
}

int
n64cart_uart_write(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    return ttwrite(&n64cart_uart_ttys[0], uio, flag);
}

int
n64cart_uart_select(dev_t dev, int rw)
{
    if (minor(dev) != 0)
        return ENXIO;
    n64cart_uart_intr();
    return ttyselect(&n64cart_uart_ttys[0], rw);
}

int
n64cart_uart_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int error;
    struct winsize *ws;

    if (minor(dev) != 0)
        return ENXIO;
    error = ttioctl(&n64cart_uart_ttys[0], cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    if (error == 0 && cmd == TIOCGWINSZ) {
        n64cart_uart_default_winsize(&n64cart_uart_ttys[0]);
        ws = (struct winsize *)addr;
        if (ws->ws_row == 0)
            ws->ws_row = 24;
        if (ws->ws_col == 0)
            ws->ws_col = 80;
    }
    return error;
}

void
n64cart_uart_intr(void)
{
    struct tty *tp = &n64cart_uart_ttys[0];
    int c;

    if ((tp->t_state & TS_ISOPEN) == 0)
        return;

    while (n64cart_uart_poll()) {
        c = n64cart_uart_getc();
        n64cart_uart_input(tp, c);
    }
}

static void
n64cart_uart_start(struct tty *tp)
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
        n64cart_uart_putc(c);
        s = spltty();
    }
    tp->t_state &= ~TS_BUSY;
    ttyowake(tp);
    splx(s);
}

int __attribute__((weak))
n64_console_poll(void)
{
    return n64cart_uart_poll();
}

int __attribute__((weak))
n64_console_getc(void)
{
    return n64cart_uart_getc();
}

void __attribute__((weak))
n64_console_putc(int ch)
{
    n64cart_uart_putc(ch);
}

void
n64_console_debug_putc(int ch)
{
    n64cart_uart_putc(ch);
}
