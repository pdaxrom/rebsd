#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/kconfig.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <machine/console.h>

#define MALTA_UART0     0xb80003f8u
#define UART_RBR        0
#define UART_THR        0
#define UART_IER        1
#define UART_FCR        2
#define UART_LCR        3
#define UART_MCR        4
#define UART_LSR        5
#define UART_DLL        0
#define UART_DLM        1
#define UART_LSR_DR     0x01
#define UART_LSR_THRE   0x20
#define UART_LCR_DLAB   0x80

struct tty malta_uart_ttys[1];
static void malta_uart_start(struct tty *tp);
static int malta_uart_esc_state;
void malta_uart_intr(void);

static volatile unsigned char *
uart_reg(unsigned offset)
{
    return (volatile unsigned char *)(MALTA_UART0 + offset);
}

static unsigned
uart_read(unsigned offset)
{
    return *uart_reg(offset);
}

static void
uart_write(unsigned offset, unsigned value)
{
    *uart_reg(offset) = value;
}

static void
malta_uart_default_winsize(struct tty *tp)
{
    if (tp->t_winsize.ws_row == 0)
        tp->t_winsize.ws_row = 24;
    if (tp->t_winsize.ws_col == 0)
        tp->t_winsize.ws_col = 80;
}

static int
malta_uart_init(void *arg)
{
    (void)arg;
    uart_write(UART_IER, 0x00);
    uart_write(UART_LCR, UART_LCR_DLAB);
    uart_write(UART_DLL, 0x01);
    uart_write(UART_DLM, 0x00);
    uart_write(UART_LCR, 0x03);
    uart_write(UART_FCR, 0x07);
    uart_write(UART_MCR, 0x03);
    return 1;
}

struct driver malta_uartdriver = {
    "malta_uart",
    malta_uart_init,
};

int
malta_uart_poll(void)
{
    return (uart_read(UART_LSR) & UART_LSR_DR) != 0;
}

int
malta_uart_getc(void)
{
    while (!malta_uart_poll())
        ;
    return uart_read(UART_RBR) & 0xff;
}

void
malta_uart_putc(int ch)
{
    while ((uart_read(UART_LSR) & UART_LSR_THRE) == 0)
        ;
    uart_write(UART_THR, ch & 0xff);
}

char
malta_uart_raw_read(dev_t dev)
{
    (void)dev;
    return malta_uart_getc();
}

void
malta_uart_raw_write(dev_t dev, char ch)
{
    (void)dev;
    malta_uart_putc(ch);
}

#define MALTA_UART_ESC_NONE     0
#define MALTA_UART_ESC_ESC      1
#define MALTA_UART_ESC_CSI      2
#define MALTA_UART_ESC_CSI_3    3

static void
malta_uart_input_normal(struct tty *tp, int c)
{
    if (c == '\b' || c == '\177')
        c = '\177';
    ttyinput(c, tp);
}

static void
malta_uart_input(struct tty *tp, int c)
{
again:
    switch (malta_uart_esc_state) {
    case MALTA_UART_ESC_ESC:
        if (c == '[') {
            malta_uart_esc_state = MALTA_UART_ESC_CSI;
            return;
        }
        ttyinput('\033', tp);
        malta_uart_esc_state = MALTA_UART_ESC_NONE;
        goto again;

    case MALTA_UART_ESC_CSI:
        if (c == '3') {
            malta_uart_esc_state = MALTA_UART_ESC_CSI_3;
            return;
        }
        ttyinput('\033', tp);
        ttyinput('[', tp);
        malta_uart_esc_state = MALTA_UART_ESC_NONE;
        goto again;

    case MALTA_UART_ESC_CSI_3:
        malta_uart_esc_state = MALTA_UART_ESC_NONE;
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
        malta_uart_esc_state = MALTA_UART_ESC_ESC;
        return;
    }
    malta_uart_input_normal(tp, c);
}

int
malta_uart_open(dev_t dev, int flag, int mode)
{
    struct tty *tp;

    if (minor(dev) != 0)
        return ENXIO;

    tp = &malta_uart_ttys[0];
    tp->t_oproc = malta_uart_start;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        tp->t_ispeed = B115200;
        tp->t_ospeed = B115200;
        ttychars(tp);
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA |
            CTLECH | CRTKIL;
    }
    malta_uart_default_winsize(tp);
    tp->t_state |= TS_CARR_ON;
    if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
        return EBUSY;

    return ttyopen(dev, tp);
}

int
malta_uart_close(dev_t dev, int flag, int mode)
{
    struct tty *tp = &malta_uart_ttys[0];

    if (minor(dev) != 0)
        return ENXIO;
    ttywflush(tp);
    ttyclose(tp);
    return 0;
}

int
malta_uart_read(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    malta_uart_intr();
    return ttread(&malta_uart_ttys[0], uio, flag);
}

int
malta_uart_write(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    return ttwrite(&malta_uart_ttys[0], uio, flag);
}

int
malta_uart_select(dev_t dev, int rw)
{
    if (minor(dev) != 0)
        return ENXIO;
    malta_uart_intr();
    return ttyselect(&malta_uart_ttys[0], rw);
}

int
malta_uart_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int error;
    struct winsize *ws;

    if (minor(dev) != 0)
        return ENXIO;
    error = ttioctl(&malta_uart_ttys[0], cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    if (error == 0 && cmd == TIOCGWINSZ) {
        malta_uart_default_winsize(&malta_uart_ttys[0]);
        ws = (struct winsize *)addr;
        if (ws->ws_row == 0)
            ws->ws_row = 24;
        if (ws->ws_col == 0)
            ws->ws_col = 80;
    }
    return error;
}

void
malta_uart_intr(void)
{
    struct tty *tp = &malta_uart_ttys[0];
    int c;

    if ((tp->t_state & TS_ISOPEN) == 0)
        return;

    while (malta_uart_poll()) {
        c = malta_uart_getc();
        malta_uart_input(tp, c);
    }
}

#ifdef MALTA_NE_ENABLED
extern void malta_nepoll(void);
#endif

void
mips_board_timer_intr(void)
{
    malta_uart_intr();
#ifdef MALTA_NE_ENABLED
    malta_nepoll();
#endif
}

static void
malta_uart_start(struct tty *tp)
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
        malta_uart_putc(c);
        s = spltty();
    }
    tp->t_state &= ~TS_BUSY;
    ttyowake(tp);
    splx(s);
}

int
mips_console_poll(void)
{
    return malta_uart_poll();
}

int
mips_console_getc(void)
{
    return malta_uart_getc();
}

void
mips_console_putc(int ch)
{
    malta_uart_putc(ch);
}

void
mips_console_debug_putc(int ch)
{
    malta_uart_putc(ch);
}

void
mips_console_winsize(struct winsize *ws)
{
    ws->ws_row = 24;
    ws->ws_col = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
}

void
mips_console_tty_winsize(struct tty *tp)
{
    if (tp->t_winsize.ws_row == 0)
        tp->t_winsize.ws_row = 24;
    if (tp->t_winsize.ws_col == 0)
        tp->t_winsize.ws_col = 80;
}
