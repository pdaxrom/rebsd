#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/kconfig.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <machine/console.h>

#define CI20_UART4      0xb0034000u
#define CI20_INTC       0xb0001000u
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
#define UART_LSR_TEMT   0x40
#define UART_IER_RDI    0x01
#define UART_LCR_DLAB   0x80
#define UART_FCR_ENABLE 0x01
#define UART_FCR_RCVR   0x02
#define UART_FCR_XMIT   0x04
#define UART_FCR_UME    0x10
#define UART_CLK        48000000u
#define UART_BAUD       115200u
#define UART_DIVISOR    ((UART_CLK + (8u * UART_BAUD)) / (16u * UART_BAUD))

#define INTC_STATUS     0x00
#define INTC_SET_MASK   0x08
#define INTC_CLEAR_MASK 0x0c
#define INTC_PENDING    0x10
#define INTC_CHIP_SIZE  0x20
#define CI20_TCU_IRQ    25
#define CI20_GPIOE_IRQ  13
#define CI20_UART4_IRQ  34
#define CI20_OHCI_IRQ   5

struct tty ci20_uart_ttys[1];
static void ci20_uart_start(struct tty *tp);
static int ci20_uart_esc_state;
static int ci20_uart_attached;
void ci20_uart_intr(void);
extern int ci20_clock_intr(int *frame, unsigned status);
#ifdef CI20_DM9000_ENABLED
extern int ci20_dm9000_intr(void);
#endif
#ifdef OHCI_ENABLED
extern int ci20_ohci_intr(void);
extern void ci20_ohci_irq_storm(void);
static unsigned ci20_ohci_irqs_since_tick;
#endif
#ifdef INET
extern int netisr;
extern void netintr(void);
#endif

static volatile unsigned char *
uart_reg(unsigned offset)
{
    return (volatile unsigned char *)(CI20_UART4 + (offset << 2));
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

static volatile unsigned *
intc_reg(unsigned irq, unsigned offset)
{
    return (volatile unsigned *)(CI20_INTC +
        (irq / 32) * INTC_CHIP_SIZE + offset);
}

static unsigned
intc_bit(unsigned irq)
{
    return 1u << (irq % 32);
}

static unsigned
intc_pending(unsigned irq)
{
    return *intc_reg(irq, INTC_PENDING) & intc_bit(irq);
}

static void
intc_unmask(unsigned irq)
{
    *intc_reg(irq, INTC_CLEAR_MASK) = intc_bit(irq);
}

static void
intc_mask(unsigned irq)
{
    *intc_reg(irq, INTC_SET_MASK) = intc_bit(irq);
}

void
ci20_intc_unmask_irq(unsigned irq)
{
    intc_unmask(irq);
}

static void
ci20_uart_enable_rx_irq(void)
{
    unsigned lcr = uart_read(UART_LCR);

    if (lcr & UART_LCR_DLAB)
        uart_write(UART_LCR, lcr & ~UART_LCR_DLAB);
    uart_write(UART_IER, UART_IER_RDI);
}

static void
ci20_intc_init(void)
{
    *(volatile unsigned *)(CI20_INTC + 0 * INTC_CHIP_SIZE + INTC_SET_MASK) =
        0xffffffffu;
    *(volatile unsigned *)(CI20_INTC + 1 * INTC_CHIP_SIZE + INTC_SET_MASK) =
        0xffffffffu;
    intc_unmask(CI20_UART4_IRQ);
}

void
ci20_uart_attach(void)
{
    if (ci20_uart_attached)
        return;
    ci20_uart_attached = 1;
    ci20_intc_init();
    ci20_uart_enable_rx_irq();
    printf("ci20 uart: irq %u enabled\n", CI20_UART4_IRQ);
}

static void
ci20_uart_default_winsize(struct tty *tp)
{
    if (tp->t_winsize.ws_row == 0)
        tp->t_winsize.ws_row = 24;
    if (tp->t_winsize.ws_col == 0)
        tp->t_winsize.ws_col = 80;
}

static int
ci20_uart_init(void *arg)
{
#ifdef CI20_UART_REINIT
    unsigned divisor = UART_DIVISOR;

    (void)arg;
    uart_write(UART_IER, 0x00);
    uart_write(UART_LCR, UART_LCR_DLAB | 0x03);
    uart_write(UART_DLL, divisor & 0xff);
    uart_write(UART_DLM, (divisor >> 8) & 0xff);
    uart_write(UART_LCR, 0x03);
    uart_write(UART_FCR, UART_FCR_UME | UART_FCR_ENABLE |
        UART_FCR_RCVR | UART_FCR_XMIT);
    uart_write(UART_MCR, 0x03);
#else
    /*
     * U-Boot has already configured UART4 for the console.  Keep that setup
     * during early Ci20 bring-up; rewriting the Ingenic UART FIFO/control
     * registers can disturb RX while the polled console is still our only I/O.
     */
    (void)arg;
#endif
    ci20_uart_attach();
    return 1;
}

struct driver ci20_uartdriver = {
    "ci20_uart",
    ci20_uart_init,
};

int
ci20_uart_poll(void)
{
    return (uart_read(UART_LSR) & UART_LSR_DR) != 0;
}

int
ci20_uart_getc(void)
{
    while (!ci20_uart_poll())
        ;
    return uart_read(UART_RBR) & 0xff;
}

void
ci20_uart_putc(int ch)
{
    while ((uart_read(UART_LSR) & UART_LSR_THRE) == 0)
        ;
    uart_write(UART_THR, ch & 0xff);
}

char
ci20_uart_raw_read(dev_t dev)
{
    (void)dev;
    return ci20_uart_getc();
}

void
ci20_uart_raw_write(dev_t dev, char ch)
{
    (void)dev;
    ci20_uart_putc(ch);
}

#define CI20_UART_ESC_NONE      0
#define CI20_UART_ESC_ESC       1
#define CI20_UART_ESC_CSI       2
#define CI20_UART_ESC_CSI_3     3

static void
ci20_uart_input_normal(struct tty *tp, int c)
{
    if (c == '\b' || c == '\177')
        c = '\177';
    ttyinput(c, tp);
}

static void
ci20_uart_input(struct tty *tp, int c)
{
again:
    switch (ci20_uart_esc_state) {
    case CI20_UART_ESC_ESC:
        if (c == '[') {
            ci20_uart_esc_state = CI20_UART_ESC_CSI;
            return;
        }
        ttyinput('\033', tp);
        ci20_uart_esc_state = CI20_UART_ESC_NONE;
        goto again;

    case CI20_UART_ESC_CSI:
        if (c == '3') {
            ci20_uart_esc_state = CI20_UART_ESC_CSI_3;
            return;
        }
        ttyinput('\033', tp);
        ttyinput('[', tp);
        ci20_uart_esc_state = CI20_UART_ESC_NONE;
        goto again;

    case CI20_UART_ESC_CSI_3:
        ci20_uart_esc_state = CI20_UART_ESC_NONE;
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
        ci20_uart_esc_state = CI20_UART_ESC_ESC;
        return;
    }
    ci20_uart_input_normal(tp, c);
}

int
ci20_uart_open(dev_t dev, int flag, int mode)
{
    struct tty *tp;

    if (minor(dev) != 0)
        return ENXIO;

    tp = &ci20_uart_ttys[0];
    tp->t_oproc = ci20_uart_start;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        tp->t_ispeed = B115200;
        tp->t_ospeed = B115200;
        ttychars(tp);
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA |
            CTLECH | CRTKIL;
    }
    ci20_uart_default_winsize(tp);
    tp->t_state |= TS_CARR_ON;
    if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
        return EBUSY;

    return ttyopen(dev, tp);
}

int
ci20_uart_close(dev_t dev, int flag, int mode)
{
    struct tty *tp = &ci20_uart_ttys[0];

    if (minor(dev) != 0)
        return ENXIO;
    ttywflush(tp);
    ttyclose(tp);
    return 0;
}

int
ci20_uart_read(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    ci20_uart_intr();
    return ttread(&ci20_uart_ttys[0], uio, flag);
}

int
ci20_uart_write(dev_t dev, struct uio *uio, int flag)
{
    if (minor(dev) != 0)
        return ENXIO;
    return ttwrite(&ci20_uart_ttys[0], uio, flag);
}

int
ci20_uart_select(dev_t dev, int rw)
{
    if (minor(dev) != 0)
        return ENXIO;
    ci20_uart_intr();
    return ttyselect(&ci20_uart_ttys[0], rw);
}

int
ci20_uart_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    int error;
    struct winsize *ws;

    if (minor(dev) != 0)
        return ENXIO;
    error = ttioctl(&ci20_uart_ttys[0], cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    if (error == 0 && cmd == TIOCGWINSZ) {
        ci20_uart_default_winsize(&ci20_uart_ttys[0]);
        ws = (struct winsize *)addr;
        if (ws->ws_row == 0)
            ws->ws_row = 24;
        if (ws->ws_col == 0)
            ws->ws_col = 80;
    }
    return error;
}

void
ci20_uart_intr(void)
{
    struct tty *tp = &ci20_uart_ttys[0];
    int c;

    if ((tp->t_state & TS_ISOPEN) == 0)
        return;

    while (ci20_uart_poll()) {
        c = ci20_uart_getc();
        ci20_uart_input(tp, c);
    }
}

static void
ci20_uart_start(struct tty *tp)
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
        ci20_uart_putc(c);
        s = spltty();
    }
    tp->t_state &= ~TS_BUSY;
    ttyowake(tp);
    splx(s);
}

void
mips_board_timer_intr(void)
{
    ci20_uart_intr();
}

void
mips_board_intr(int *frame, unsigned status)
{
    if (intc_pending(CI20_TCU_IRQ)) {
        ci20_clock_intr(frame, status);
#ifdef OHCI_ENABLED
        ci20_ohci_irqs_since_tick = 0;
#endif
    }
#ifdef OHCI_ENABLED
    if (intc_pending(CI20_OHCI_IRQ)) {
        if (++ci20_ohci_irqs_since_tick > 32) {
            intc_mask(CI20_OHCI_IRQ);
            ci20_ohci_irq_storm();
        } else
            (void)ci20_ohci_intr();
    }
#endif
#ifdef CI20_DM9000_ENABLED
    if (intc_pending(CI20_GPIOE_IRQ) && ci20_dm9000_intr()) {
#ifdef INET
        if (netisr)
            netintr();
#endif
    }
#endif
    if (intc_pending(CI20_UART4_IRQ))
        ci20_uart_intr();
}

int
mips_console_poll(void)
{
    return ci20_uart_poll();
}

int
mips_console_getc(void)
{
    return ci20_uart_getc();
}

void
mips_console_putc(int ch)
{
    ci20_uart_putc(ch);
}

void
mips_console_debug_putc(int ch)
{
    ci20_uart_putc(ch);
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
