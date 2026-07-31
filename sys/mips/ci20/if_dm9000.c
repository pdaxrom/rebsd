/*
 * Davicom DM9000 Ethernet driver for Creator Ci20.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>

#define CI20_CPM                0xb0000000u
#define CI20_GPIO               0xb0010000u

#define CPM_CLKGR0              0x20
#define CPM_CLKGR0_MAC          (1u << 23)
#define CPM_CLKGR0_NEMC         (1u << 0)

#define GPIO_PXINTC(n)          (0x18 + (n) * 0x100)
#define GPIO_PXMASKS(n)         (0x24 + (n) * 0x100)
#define GPIO_PXMASKC(n)         (0x28 + (n) * 0x100)
#define GPIO_PXINTS(n)          (0x14 + (n) * 0x100)
#define GPIO_PXPAT1S(n)         (0x34 + (n) * 0x100)
#define GPIO_PXPAT1C(n)         (0x38 + (n) * 0x100)
#define GPIO_PXPAT0S(n)         (0x44 + (n) * 0x100)
#define GPIO_PXPAT0C(n)         (0x48 + (n) * 0x100)
#define GPIO_PXFLG(n)           (0x50 + (n) * 0x100)
#define GPIO_PXFLGC(n)          (0x58 + (n) * 0x100)
#define GPIO_PXPENS(n)          (0x74 + (n) * 0x100)

#define CI20_DM9000_IRQ_PORT    4
#define CI20_DM9000_IRQ_PIN     19
#define CI20_DM9000_INTC_IRQ    13

#define DM9000_IO_ADDR          0xb6000000u
#define DM9000_DATA_ADDR        (DM9000_IO_ADDR + 2)

#define DM9000_ID               0x90000a46u
#define DM9000_PKT_RDY          0x01
#define DM9000_PKT_MAX          1536
#define DM9000_TX_MAX           1600

#define DM9000_NCR              0x00
#define DM9000_NSR              0x01
#define DM9000_TCR              0x02
#define DM9000_RCR              0x05
#define DM9000_BPTR             0x08
#define DM9000_FCTR             0x09
#define DM9000_FCR              0x0a
#define DM9000_EPCR             0x0b
#define DM9000_EPAR             0x0c
#define DM9000_EPDRL            0x0d
#define DM9000_EPDRH            0x0e
#define DM9000_PAR              0x10
#define DM9000_MAR              0x16
#define DM9000_GPCR             0x1e
#define DM9000_GPR              0x1f
#define DM9000_VIDL             0x28
#define DM9000_VIDH             0x29
#define DM9000_PIDL             0x2a
#define DM9000_PIDH             0x2b
#define DM9000_SMCR             0x2f
#define DM9000_MRCMDX           0xf0
#define DM9000_MRCMD            0xf2
#define DM9000_MWCMD            0xf8
#define DM9000_TXPLL            0xfc
#define DM9000_TXPLH            0xfd
#define DM9000_ISR              0xfe
#define DM9000_IMR              0xff

#define NCR_LBK_INT_MAC         (1u << 1)
#define NCR_RST                 (1u << 0)

#define NSR_WAKEST              (1u << 5)
#define NSR_TX2END              (1u << 3)
#define NSR_TX1END              (1u << 2)
#define NSR_RXOV                (1u << 1)

#define TCR_TXREQ               (1u << 0)

#define RCR_DIS_LONG            (1u << 5)
#define RCR_DIS_CRC             (1u << 4)
#define RCR_RXEN                (1u << 0)

#define BPTR_BPHW(x)            ((x) << 4)
#define BPTR_JPT_600US          0x0f
#define FCTR_HWOT(x)            (((x) & 0x0f) << 4)
#define FCTR_LWOT(x)            ((x) & 0x0f)

#define GPCR_GPIO0_OUT          (1u << 0)

#define ISR_ROOS                (1u << 3)
#define ISR_ROS                 (1u << 2)
#define ISR_PTS                 (1u << 1)
#define ISR_PRS                 (1u << 0)
#define ISR_BUS_MODE_MASK       (3u << 6)

#define IMR_PAR                 (1u << 7)
#define IMR_PTM                 (1u << 1)
#define IMR_PRM                 (1u << 0)
#define DM9000_IMR_ENABLE       (IMR_PAR | IMR_PTM | IMR_PRM)

#define DM9000_BUS_16           16
#define DM9000_BUS_8            8

struct dm9000_softc {
    struct arpcom sc_ac;
#define sc_if sc_ac.ac_if
    int sc_present;
    int sc_hw_ready;
    int sc_tx_busy;
    int sc_bus_width;
    int sc_irq_enabled;
    unsigned long sc_irq_calls;
    unsigned long sc_irq_events;
    unsigned long sc_irq_empty;
    unsigned long sc_irq_looplimit;
    unsigned long sc_polls;
    unsigned long sc_empty_polls;
    unsigned long sc_rx_irq;
    unsigned long sc_tx_irq;
    unsigned long sc_over_irq;
    unsigned long sc_rx_frames;
    unsigned long sc_rx_eof;
    unsigned long sc_rx_badready;
    unsigned long sc_rx_badstatus;
    unsigned long sc_icmp_rx;
    unsigned long sc_icmp_slow;
    unsigned long sc_icmp_last_seq;
    unsigned long sc_icmp_last_rtt_us;
    unsigned long sc_icmp_max_seq;
    unsigned long sc_icmp_max_rtt_us;
    unsigned long sc_icmp_slow_seq;
    unsigned long sc_icmp_slow_rtt_us;
    unsigned char sc_txbuf[DM9000_TX_MAX];
};

static struct dm9000_softc dm9000_softc[1];

static int dm9000output(struct ifnet *ifp, struct mbuf *m0,
    struct sockaddr *dst);
static int dm9000ioctl(struct ifnet *ifp, int cmd, caddr_t data);
static int dm9000init(int unit);
static int dm9000start(int unit);
static int dm9000watchdog(int unit);
static void dm9000poll(int unit);
static unsigned dm9000recv(struct dm9000_softc *sc);
static void dm9000_trace_icmp(struct dm9000_softc *sc, unsigned char *frame,
    unsigned frame_len);
static int dm9000_hw_probe(struct dm9000_softc *sc);
static void dm9000_chip_init(struct dm9000_softc *sc);
static void dm9000_stop(struct dm9000_softc *sc);
static void ci20_dm9000_irq_setup(struct dm9000_softc *sc);
static char *dm9000_stats_puts(char *p, char *end, const char *s);
static char *dm9000_stats_putul(char *p, char *end, unsigned long value);
static char *dm9000_stats_putkv(char *p, char *end, const char *key,
    unsigned long value);

extern void udelay(unsigned usec);
extern void ci20_intc_unmask_irq(unsigned irq);

static volatile unsigned *
ci20_reg(unsigned base, unsigned offset)
{
    return (volatile unsigned *)(base + offset);
}

static unsigned
ci20_read(unsigned base, unsigned offset)
{
    return *ci20_reg(base, offset);
}

static void
ci20_write(unsigned base, unsigned offset, unsigned value)
{
    *ci20_reg(base, offset) = value;
}

static void
ci20_clock_enable(unsigned mask)
{
    ci20_write(CI20_CPM, CPM_CLKGR0, ci20_read(CI20_CPM, CPM_CLKGR0) & ~mask);
}

static void
ci20_gpio_write(int port, int pin, int value)
{
    ci20_write(CI20_GPIO, value ? GPIO_PXPAT0S(port) : GPIO_PXPAT0C(port),
        1u << pin);
}

static void
ci20_gpio_output(int port, int pin, int value)
{
    ci20_write(CI20_GPIO, GPIO_PXINTC(port), 1u << pin);
    ci20_write(CI20_GPIO, GPIO_PXMASKS(port), 1u << pin);
    ci20_write(CI20_GPIO, GPIO_PXPAT1C(port), 1u << pin);
    ci20_gpio_write(port, pin, value);
}

static void
ci20_dm9000_pinmux(void)
{
    ci20_write(CI20_GPIO, GPIO_PXINTC(0), 0x04030000u);
    ci20_write(CI20_GPIO, GPIO_PXMASKC(0), 0x04030000u);
    ci20_write(CI20_GPIO, GPIO_PXPAT1C(0), 0x04030000u);
    ci20_write(CI20_GPIO, GPIO_PXPAT0C(0), 0x04030000u);
    ci20_write(CI20_GPIO, GPIO_PXPENS(0), 0x04030000u);
}

static unsigned
ci20_dm9000_irq_bit(void)
{
    return 1u << CI20_DM9000_IRQ_PIN;
}

static void
ci20_dm9000_gpio_irq_ack(void)
{
    ci20_write(CI20_GPIO, GPIO_PXFLGC(CI20_DM9000_IRQ_PORT),
        ci20_dm9000_irq_bit());
}

static void
ci20_dm9000_gpio_irq_mask(void)
{
    ci20_write(CI20_GPIO, GPIO_PXMASKS(CI20_DM9000_IRQ_PORT),
        ci20_dm9000_irq_bit());
}

static void
ci20_dm9000_gpio_irq_unmask(void)
{
    ci20_write(CI20_GPIO, GPIO_PXMASKC(CI20_DM9000_IRQ_PORT),
        ci20_dm9000_irq_bit());
}

static int
ci20_dm9000_gpio_irq_pending(void)
{
    return (ci20_read(CI20_GPIO, GPIO_PXFLG(CI20_DM9000_IRQ_PORT)) &
        ci20_dm9000_irq_bit()) != 0;
}

static void
ci20_dm9000_irq_setup(struct dm9000_softc *sc)
{
    unsigned bit = ci20_dm9000_irq_bit();

    ci20_dm9000_gpio_irq_mask();
    ci20_write(CI20_GPIO, GPIO_PXINTS(CI20_DM9000_IRQ_PORT), bit);
    ci20_write(CI20_GPIO, GPIO_PXPAT1C(CI20_DM9000_IRQ_PORT), bit);
    ci20_write(CI20_GPIO, GPIO_PXPAT0S(CI20_DM9000_IRQ_PORT), bit);
    ci20_dm9000_gpio_irq_ack();
    ci20_dm9000_gpio_irq_unmask();
    ci20_intc_unmask_irq(CI20_DM9000_INTC_IRQ);

    if (!sc->sc_irq_enabled) {
        sc->sc_irq_enabled = 1;
        printf("dm0: irq gpe%u level-high intc %u enabled\n",
            CI20_DM9000_IRQ_PIN, CI20_DM9000_INTC_IRQ);
    }
}

static char *
dm9000_stats_puts(char *p, char *end, const char *s)
{
    while (p < end && *s)
        *p++ = *s++;
    return p;
}

static char *
dm9000_stats_putul(char *p, char *end, unsigned long value)
{
    char tmp[10 * sizeof(unsigned long)];
    int n = 0;

    do {
        tmp[n++] = '0' + value % 10;
        value /= 10;
    } while (value && n < sizeof(tmp));

    while (p < end && n > 0)
        *p++ = tmp[--n];
    return p;
}

static char *
dm9000_stats_putkv(char *p, char *end, const char *key, unsigned long value)
{
    p = dm9000_stats_puts(p, end, key);
    if (p < end)
        *p++ = '=';
    p = dm9000_stats_putul(p, end, value);
    if (p < end)
        *p++ = ' ';
    return p;
}

int
ci20_dm9000_stats(char *buf, int len)
{
    struct dm9000_softc *sc = &dm9000_softc[0];
    char *p, *end;
    int s;

    if (len <= 0)
        return 0;

    p = buf;
    end = buf + len - 1;

    s = splimp();
    p = dm9000_stats_putkv(p, end, "irq_calls", sc->sc_irq_calls);
    p = dm9000_stats_putkv(p, end, "irq_events", sc->sc_irq_events);
    p = dm9000_stats_putkv(p, end, "irq_empty", sc->sc_irq_empty);
    p = dm9000_stats_putkv(p, end, "irq_limit", sc->sc_irq_looplimit);
    p = dm9000_stats_putkv(p, end, "polls", sc->sc_polls);
    p = dm9000_stats_putkv(p, end, "empty_polls", sc->sc_empty_polls);
    p = dm9000_stats_putkv(p, end, "rx_irq", sc->sc_rx_irq);
    p = dm9000_stats_putkv(p, end, "tx_irq", sc->sc_tx_irq);
    p = dm9000_stats_putkv(p, end, "over_irq", sc->sc_over_irq);
    p = dm9000_stats_putkv(p, end, "rx_frames", sc->sc_rx_frames);
    p = dm9000_stats_putkv(p, end, "rx_eof", sc->sc_rx_eof);
    p = dm9000_stats_putkv(p, end, "rx_badready", sc->sc_rx_badready);
    p = dm9000_stats_putkv(p, end, "rx_badstatus", sc->sc_rx_badstatus);
    p = dm9000_stats_putkv(p, end, "icmp_rx", sc->sc_icmp_rx);
    p = dm9000_stats_putkv(p, end, "icmp_slow", sc->sc_icmp_slow);
    p = dm9000_stats_putkv(p, end, "icmp_last_seq",
        sc->sc_icmp_last_seq);
    p = dm9000_stats_putkv(p, end, "icmp_last_us",
        sc->sc_icmp_last_rtt_us);
    p = dm9000_stats_putkv(p, end, "icmp_max_seq", sc->sc_icmp_max_seq);
    p = dm9000_stats_putkv(p, end, "icmp_max_us",
        sc->sc_icmp_max_rtt_us);
    p = dm9000_stats_putkv(p, end, "icmp_slow_seq",
        sc->sc_icmp_slow_seq);
    p = dm9000_stats_putkv(p, end, "icmp_slow_us",
        sc->sc_icmp_slow_rtt_us);
    p = dm9000_stats_putkv(p, end, "ipkts", sc->sc_if.if_ipackets);
    p = dm9000_stats_putkv(p, end, "opkts", sc->sc_if.if_opackets);
    p = dm9000_stats_putkv(p, end, "ierr", sc->sc_if.if_ierrors);
    p = dm9000_stats_putkv(p, end, "oerr", sc->sc_if.if_oerrors);
    splx(s);

    if (p > buf && p[-1] == ' ')
        p--;
    *p = 0;
    return 0;
}

static unsigned long
dm9000_rtt_us(struct timeval *now, struct timeval *sent)
{
    long sec;
    long usec;

    sec = now->tv_sec - sent->tv_sec;
    usec = now->tv_usec - sent->tv_usec;
    if (usec < 0) {
        usec += 1000000L;
        sec--;
    }
    if (sec < 0 || sec > 3600)
        return 0;
    return (unsigned long)sec * 1000000UL + (unsigned long)usec;
}

static void
dm9000_trace_icmp(struct dm9000_softc *sc, unsigned char *frame,
    unsigned frame_len)
{
    struct timeval now, sent;
    unsigned ipoff, iphlen, icmpoff, seq;
    u_short seq16;
    unsigned long rtt;

    ipoff = sizeof(struct ether_header);
    if (frame_len < ipoff + 20)
        return;
    if ((frame[ipoff] >> 4) != IPVERSION)
        return;
    iphlen = (frame[ipoff] & 0x0f) << 2;
    if (iphlen < 20 || frame_len < ipoff + iphlen + ICMP_MINLEN)
        return;
    if (frame[ipoff + 9] != IPPROTO_ICMP)
        return;

    icmpoff = ipoff + iphlen;
    if (frame[icmpoff] != ICMP_ECHOREPLY || frame[icmpoff + 1] != 0)
        return;
    if (frame_len < icmpoff + ICMP_MINLEN + sizeof(struct timeval))
        return;

    bcopy((caddr_t)&frame[icmpoff + 6], (caddr_t)&seq16, sizeof(seq16));
    seq = seq16;
    bcopy((caddr_t)&frame[icmpoff + ICMP_MINLEN], (caddr_t)&sent,
        sizeof(sent));
    microtime(&now);
    rtt = dm9000_rtt_us(&now, &sent);
    if (rtt == 0)
        return;

    sc->sc_icmp_rx++;
    sc->sc_icmp_last_seq = seq;
    sc->sc_icmp_last_rtt_us = rtt;
    if (rtt > sc->sc_icmp_max_rtt_us) {
        sc->sc_icmp_max_rtt_us = rtt;
        sc->sc_icmp_max_seq = seq;
    }
    if (rtt >= 10000UL) {
        sc->sc_icmp_slow++;
        sc->sc_icmp_slow_seq = seq;
        sc->sc_icmp_slow_rtt_us = rtt;
    }
}

static volatile unsigned char *
dm9000_io8(void)
{
    return (volatile unsigned char *)DM9000_IO_ADDR;
}

static volatile unsigned char *
dm9000_data8(void)
{
    return (volatile unsigned char *)DM9000_DATA_ADDR;
}

static volatile unsigned short *
dm9000_data16(void)
{
    return (volatile unsigned short *)DM9000_DATA_ADDR;
}

static unsigned
dm9000_read(unsigned reg)
{
    *dm9000_io8() = reg & 0xff;
    return *dm9000_data8() & 0xff;
}

static void
dm9000_write(unsigned reg, unsigned value)
{
    *dm9000_io8() = reg & 0xff;
    *dm9000_data8() = value & 0xff;
}

static void
dm9000_write_fifo8(const unsigned char *buf, unsigned count)
{
    volatile unsigned char *data = dm9000_data8();
    unsigned i;

    for (i = 0; i < count; i++)
        *data = buf[i];
}

static void
dm9000_read_fifo8(unsigned char *buf, unsigned count)
{
    volatile unsigned char *data = dm9000_data8();
    unsigned i;

    for (i = 0; i < count; i++)
        buf[i] = *data;
}

static void
dm9000_write_fifo16(const unsigned char *buf, unsigned count)
{
    volatile unsigned short *data = dm9000_data16();
    unsigned value;

    while (count >= 2) {
        value = buf[0] | (buf[1] << 8);
        *data = value;
        buf += 2;
        count -= 2;
    }
    if (count)
        *data = buf[0];
}

static void
dm9000_read_fifo16(unsigned char *buf, unsigned count)
{
    volatile unsigned short *data = dm9000_data16();
    unsigned value;

    while (count >= 2) {
        value = *data;
        buf[0] = value & 0xff;
        buf[1] = (value >> 8) & 0xff;
        buf += 2;
        count -= 2;
    }
    if (count) {
        value = *data;
        buf[0] = value & 0xff;
    }
}

static void
dm9000_write_fifo(struct dm9000_softc *sc, const unsigned char *buf,
    unsigned count)
{
    *dm9000_io8() = DM9000_MWCMD;
    if (sc->sc_bus_width == DM9000_BUS_16)
        dm9000_write_fifo16(buf, count);
    else
        dm9000_write_fifo8(buf, count);
}

static void
dm9000_read_fifo(struct dm9000_softc *sc, unsigned char *buf, unsigned count)
{
    if (sc->sc_bus_width == DM9000_BUS_16)
        dm9000_read_fifo16(buf, count);
    else
        dm9000_read_fifo8(buf, count);
}

static unsigned
dm9000_read_word_data(struct dm9000_softc *sc)
{
    unsigned lo, hi;

    if (sc->sc_bus_width == DM9000_BUS_16)
        return *dm9000_data16() & 0xffff;
    lo = *dm9000_data8() & 0xff;
    hi = *dm9000_data8() & 0xff;
    return lo | (hi << 8);
}

static void
dm9000_rx_status(struct dm9000_softc *sc, unsigned *status, unsigned *len)
{
    *dm9000_io8() = DM9000_MRCMD;
    *status = dm9000_read_word_data(sc);
    *len = dm9000_read_word_data(sc);
}

static unsigned
dm9000_id(void)
{
    return dm9000_read(DM9000_VIDL) |
        (dm9000_read(DM9000_VIDH) << 8) |
        (dm9000_read(DM9000_PIDL) << 16) |
        (dm9000_read(DM9000_PIDH) << 24);
}

static int
dm9000_detect_bus(struct dm9000_softc *sc)
{
    unsigned mode = dm9000_read(DM9000_ISR) & ISR_BUS_MODE_MASK;

    switch (mode >> 6) {
    case 0:
        sc->sc_bus_width = DM9000_BUS_16;
        return 1;
    case 2:
        sc->sc_bus_width = DM9000_BUS_8;
        return 1;
    default:
        printf("dm0: unsupported bus mode %u at 0x%x\n",
            mode >> 6, DM9000_IO_ADDR);
        return 0;
    }
}

static int
dm9000_reset(void)
{
    int tries;

    dm9000_write(DM9000_GPCR, GPCR_GPIO0_OUT);
    dm9000_write(DM9000_GPR, 0);
    dm9000_write(DM9000_NCR, NCR_LBK_INT_MAC | NCR_RST);
    for (tries = 1000; tries > 0; tries--) {
        if ((dm9000_read(DM9000_NCR) & NCR_RST) == 0)
            break;
        udelay(25);
    }
    if (tries == 0)
        return 0;

    dm9000_write(DM9000_NCR, 0);
    dm9000_write(DM9000_NCR, NCR_LBK_INT_MAC | NCR_RST);
    for (tries = 1000; tries > 0; tries--) {
        if ((dm9000_read(DM9000_NCR) & NCR_RST) == 0)
            break;
        udelay(25);
    }
    if (tries == 0)
        return 0;

    return dm9000_id() == DM9000_ID;
}

static int
dm9000_valid_enaddr(const unsigned char *enaddr)
{
    int i, allzero = 1, allff = 1;

    for (i = 0; i < 6; i++) {
        if (enaddr[i] != 0x00)
            allzero = 0;
        if (enaddr[i] != 0xff)
            allff = 0;
    }
    return !allzero && !allff && (enaddr[0] & 1) == 0;
}

static void
dm9000_get_enaddr(unsigned char *enaddr)
{
    int i;

    for (i = 0; i < 6; i++)
        enaddr[i] = dm9000_read(DM9000_PAR + i);
    if (dm9000_valid_enaddr(enaddr))
        return;

    enaddr[0] = 0x02;
    enaddr[1] = 0x20;
    enaddr[2] = 0x00;
    enaddr[3] = 0x00;
    enaddr[4] = 0x00;
    enaddr[5] = 0x20;
}

static void
dm9000_fallback_enaddr(unsigned char *enaddr)
{
    enaddr[0] = 0x02;
    enaddr[1] = 0x20;
    enaddr[2] = 0x00;
    enaddr[3] = 0x00;
    enaddr[4] = 0x00;
    enaddr[5] = 0x20;
}

static int
dm9000_hw_probe(struct dm9000_softc *sc)
{
    unsigned id;

    if (sc->sc_hw_ready)
        return 1;

    ci20_dm9000_pinmux();
    ci20_clock_enable(CPM_CLKGR0_MAC | CPM_CLKGR0_NEMC);
    ci20_gpio_output(1, 25, 1);
    if (!dm9000_reset())
        printf("dm0: reset warning, probing id anyway\n");
    id = dm9000_id();
    if (id != DM9000_ID) {
        printf("dm0: not found at 0x%x id=0x%x\n", DM9000_IO_ADDR, id);
        return 0;
    }
    if (!dm9000_detect_bus(sc))
        return 0;

    dm9000_get_enaddr(sc->sc_ac.ac_enaddr);
    sc->sc_hw_ready = 1;
    printf("dm0: dm9000 id=0x%x %d-bit address %s\n", id,
        sc->sc_bus_width, ether_sprintf(sc->sc_ac.ac_enaddr));
    return 1;
}

static void
dm9000_chip_init(struct dm9000_softc *sc)
{
    int i;

    dm9000_write(DM9000_IMR, IMR_PAR);
    dm9000_write(DM9000_NCR, 0);
    dm9000_write(DM9000_TCR, 0);
    dm9000_write(DM9000_BPTR, BPTR_BPHW(3) | BPTR_JPT_600US);
    dm9000_write(DM9000_FCTR, FCTR_HWOT(3) | FCTR_LWOT(8));
    dm9000_write(DM9000_FCR, 0);
    dm9000_write(DM9000_SMCR, 0);
    dm9000_write(DM9000_NSR, NSR_WAKEST | NSR_TX2END | NSR_TX1END);
    dm9000_write(DM9000_ISR, ISR_ROOS | ISR_ROS | ISR_PTS | ISR_PRS);

    for (i = 0; i < 6; i++)
        dm9000_write(DM9000_PAR + i, sc->sc_ac.ac_enaddr[i]);
    for (i = 0; i < 8; i++)
        dm9000_write(DM9000_MAR + i, 0xff);

    dm9000_write(DM9000_RCR, RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN);
    ci20_dm9000_irq_setup(sc);
    dm9000_write(DM9000_IMR, DM9000_IMR_ENABLE);
}

static void
dm9000_stop(struct dm9000_softc *sc)
{
    unsigned reg_save;

    if (!sc->sc_hw_ready)
        return;

    reg_save = *dm9000_io8();
    dm9000_write(DM9000_IMR, IMR_PAR);
    dm9000_write(DM9000_RCR, 0);
    dm9000_write(DM9000_ISR, ISR_ROOS | ISR_ROS | ISR_PTS | ISR_PRS);
    dm9000_write(DM9000_NSR, NSR_WAKEST | NSR_TX2END | NSR_TX1END);
    ci20_dm9000_gpio_irq_ack();
    *dm9000_io8() = reg_save;

    sc->sc_tx_busy = 0;
    sc->sc_if.if_timer = 0;
}

void
ci20_dm9000attach(int unit)
{
    struct dm9000_softc *sc = &dm9000_softc[0];
    struct ifnet *ifp = &sc->sc_if;

    (void)unit;
    dm9000_fallback_enaddr(sc->sc_ac.ac_enaddr);
    sc->sc_present = 1;
    sc->sc_hw_ready = 0;
    sc->sc_bus_width = DM9000_BUS_8;
    ifp->if_name = "dm";
    ifp->if_unit = 0;
    ifp->if_mtu = ETHERMTU;
    ifp->if_flags = IFF_BROADCAST | IFF_NOTRAILERS;
    ifp->if_init = dm9000init;
    ifp->if_output = dm9000output;
    ifp->if_ioctl = dm9000ioctl;
    ifp->if_watchdog = dm9000watchdog;
    if_attach(ifp);
    printf("dm0: dm9000 deferred probe at 0x%x address %s\n", DM9000_IO_ADDR,
        ether_sprintf(sc->sc_ac.ac_enaddr));
}

static int
dm9000init(int unit)
{
    struct dm9000_softc *sc = &dm9000_softc[unit];

    if (!sc->sc_present)
        return 0;
    if (!dm9000_hw_probe(sc)) {
        sc->sc_if.if_flags &= ~IFF_RUNNING;
        return 0;
    }

    dm9000_chip_init(sc);
    sc->sc_tx_busy = 0;
    sc->sc_if.if_flags |= IFF_RUNNING;
    dm9000start(unit);
    return 0;
}

static int
dm9000ioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
    struct dm9000_softc *sc = &dm9000_softc[ifp->if_unit];
    struct in_ifaddr *ia = (struct in_ifaddr *)data;
    int s, error = 0;

    s = splimp();
    switch (cmd) {
    case SIOCGIFHWADDR:
        ((struct ifreq *)data)->ifr_addr.sa_family = AF_UNSPEC;
        bzero((caddr_t)((struct ifreq *)data)->ifr_addr.sa_data,
            sizeof(((struct ifreq *)data)->ifr_addr.sa_data));
        bcopy((caddr_t)sc->sc_ac.ac_enaddr,
            (caddr_t)((struct ifreq *)data)->ifr_addr.sa_data,
            sizeof(sc->sc_ac.ac_enaddr));
        break;

    case SIOCSIFADDR:
        ifp->if_flags |= IFF_UP;
        sc->sc_ac.ac_ipaddr = IA_SIN(ia)->sin_addr;
        dm9000init(ifp->if_unit);
        break;

    case SIOCSIFFLAGS:
        if ((ifp->if_flags & IFF_UP) == 0) {
            dm9000_stop(sc);
            ifp->if_flags &= ~IFF_RUNNING;
        } else if ((ifp->if_flags & IFF_RUNNING) == 0)
            dm9000init(ifp->if_unit);
        break;

    default:
        error = EINVAL;
        break;
    }
    splx(s);
    return error;
}

static int
dm9000output(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst)
{
    struct dm9000_softc *sc = &dm9000_softc[ifp->if_unit];

    return ether_output_enqueue(&sc->sc_ac, m0, dst, dm9000start);
}

static int
dm9000start(int unit)
{
    struct dm9000_softc *sc = &dm9000_softc[unit];
    struct mbuf *m, *n;
    unsigned count = 0;
    unsigned mincount;
    int len;

    if (sc->sc_tx_busy)
        return 0;
    if (!sc->sc_hw_ready)
        return 0;
    IF_DEQUEUE(&sc->sc_if.if_snd, m);
    if (m == 0)
        return 0;

    while (m) {
        len = m->m_len;
        if (count + len > sizeof(sc->sc_txbuf))
            len = sizeof(sc->sc_txbuf) - count;
        if (len > 0) {
            bcopy(mtod(m, caddr_t), (caddr_t)&sc->sc_txbuf[count], len);
            count += len;
        }
        MFREE(m, n);
        m = n;
    }
    mincount = ETHERMIN + sizeof(struct ether_header);
    if (count < mincount) {
        bzero((caddr_t)&sc->sc_txbuf[count], mincount - count);
        count = mincount;
    }

    sc->sc_tx_busy = 1;
    dm9000_write(DM9000_ISR, ISR_PTS);
    dm9000_write_fifo(sc, sc->sc_txbuf, count);
    dm9000_write(DM9000_TXPLL, count & 0xff);
    dm9000_write(DM9000_TXPLH, (count >> 8) & 0xff);
    dm9000_write(DM9000_TCR, TCR_TXREQ);
    sc->sc_if.if_timer = 2;
    return 0;
}

static int
dm9000watchdog(int unit)
{
    struct dm9000_softc *sc = &dm9000_softc[unit];

    dm9000poll(unit);
    if (sc->sc_tx_busy) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_oerrors++;
        dm9000_chip_init(sc);
        dm9000start(unit);
    }
    return 0;
}

static void
dm9000poll(int unit)
{
    struct dm9000_softc *sc = &dm9000_softc[unit];
    unsigned reg_save;
    unsigned isr, nsr;

    if (!sc->sc_present || !sc->sc_hw_ready ||
        (sc->sc_if.if_flags & IFF_RUNNING) == 0)
        return;

    reg_save = *dm9000_io8();
    dm9000_write(DM9000_IMR, IMR_PAR);
    isr = dm9000_read(DM9000_ISR);
    nsr = dm9000_read(DM9000_NSR);
    sc->sc_polls++;
    if ((isr & (ISR_ROOS | ISR_ROS | ISR_PTS | ISR_PRS)) == 0 &&
        (nsr & (NSR_WAKEST | NSR_TX2END | NSR_TX1END | NSR_RXOV)) == 0)
        sc->sc_empty_polls++;
    if (isr & ISR_PRS)
        sc->sc_rx_irq++;
    if ((isr & ISR_PTS) || (nsr & (NSR_TX2END | NSR_TX1END)))
        sc->sc_tx_irq++;
    if ((isr & (ISR_ROOS | ISR_ROS)) || (nsr & NSR_RXOV))
        sc->sc_over_irq++;
    if (isr & (ISR_ROOS | ISR_ROS | ISR_PTS | ISR_PRS))
        dm9000_write(DM9000_ISR, isr & (ISR_ROOS | ISR_ROS | ISR_PTS |
            ISR_PRS));
    if (nsr & (NSR_WAKEST | NSR_TX2END | NSR_TX1END))
        dm9000_write(DM9000_NSR, nsr & (NSR_WAKEST | NSR_TX2END |
            NSR_TX1END));

    if (isr & (ISR_ROOS | ISR_ROS) || nsr & NSR_RXOV)
        sc->sc_if.if_ierrors++;
    if (sc->sc_tx_busy &&
        ((isr & ISR_PTS) || (nsr & (NSR_TX2END | NSR_TX1END)))) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_timer = 0;
        sc->sc_if.if_opackets++;
        dm9000start(unit);
    }
    if (isr & ISR_PRS)
        sc->sc_rx_frames += dm9000recv(sc);

    dm9000_write(DM9000_IMR, DM9000_IMR_ENABLE);
    *dm9000_io8() = reg_save;
}

int
ci20_dm9000_intr(void)
{
    struct dm9000_softc *sc = &dm9000_softc[0];
    int handled = 0;
    int limit = 16;

    sc->sc_irq_calls++;
    ci20_dm9000_gpio_irq_mask();
    while (limit-- > 0 && ci20_dm9000_gpio_irq_pending()) {
        sc->sc_irq_events++;
        ci20_dm9000_gpio_irq_ack();
        dm9000poll(0);
        handled = 1;
    }
    if (!handled)
        sc->sc_irq_empty++;
    if (limit < 0 && ci20_dm9000_gpio_irq_pending())
        sc->sc_irq_looplimit++;
    ci20_dm9000_gpio_irq_ack();
    ci20_dm9000_gpio_irq_unmask();
    return handled;
}

static unsigned
dm9000recv(struct dm9000_softc *sc)
{
    struct ether_header *eh;
    unsigned char frame[DM9000_PKT_MAX];
    unsigned status, len, rxbyte;
    unsigned received = 0;
    int type;

    for (;;) {
        (void)dm9000_read(DM9000_MRCMDX);
        rxbyte = *dm9000_data8() & 0x03;
        if (rxbyte > DM9000_PKT_RDY) {
            dm9000_write(DM9000_RCR, 0);
            dm9000_write(DM9000_IMR, DM9000_IMR_ENABLE);
            sc->sc_rx_badready++;
            sc->sc_if.if_ierrors++;
            return received;
        }
        if (rxbyte != DM9000_PKT_RDY) {
            sc->sc_rx_eof++;
            return received;
        }

        dm9000_rx_status(sc, &status, &len);
        if ((status & 0xbf00) || len < sizeof(struct ether_header) ||
            len > sizeof(frame)) {
            sc->sc_rx_badstatus++;
            sc->sc_if.if_ierrors++;
            if (len > sizeof(frame)) {
                dm9000_chip_init(sc);
                return received;
            }
            if (len > 0 && len <= sizeof(frame))
                dm9000_read_fifo(sc, frame, len);
            continue;
        }

        dm9000_read_fifo(sc, frame, len);
        received++;
        eh = (struct ether_header *)frame;
        type = ntohs((u_short)eh->ether_type);
        if (type == ETHERTYPE_IP)
            dm9000_trace_icmp(sc, frame, len);
        (void)ether_input_frame(&sc->sc_ac, frame, len);
    }
}
