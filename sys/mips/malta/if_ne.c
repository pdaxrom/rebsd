/*
 * Minimal NE2000 ISA Ethernet driver for QEMU Malta.
 *
 * This is intentionally polling-only for the first virtual-NIC pass: Malta
 * timer interrupts already work, while ISA interrupt routing/PIC setup is not
 * part of the current board support yet.
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

#include <net/if.h>
#include <net/netisr.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <machine/layout.h>

#define NE_IO_PHYS_BASE         0x18000000u
#define NE_IO_BASE              0x300u
#define NE_IO_ADDR              ((volatile unsigned char *) \
                                (0xa0000000u | (NE_IO_PHYS_BASE + NE_IO_BASE)))

#define NE_NIC_SIZE             0x20
#define NE_DATA                 0x10
#define NE_RESET                0x1f

#define ED_P0_CR                0x00
#define ED_P0_PSTART            0x01
#define ED_P0_PSTOP             0x02
#define ED_P0_BNRY              0x03
#define ED_P0_TPSR              0x04
#define ED_P0_TBCR0             0x05
#define ED_P0_TBCR1             0x06
#define ED_P0_ISR               0x07
#define ED_P0_RSAR0             0x08
#define ED_P0_RSAR1             0x09
#define ED_P0_RBCR0             0x0a
#define ED_P0_RBCR1             0x0b
#define ED_P0_RCR               0x0c
#define ED_P0_TCR               0x0d
#define ED_P0_DCR               0x0e
#define ED_P0_IMR               0x0f

#define ED_P1_PAR0              0x01
#define ED_P1_CURR              0x07
#define ED_P1_MAR0              0x08

#define ED_CR_STP               0x01
#define ED_CR_STA               0x02
#define ED_CR_TXP               0x04
#define ED_CR_RD0               0x08
#define ED_CR_RD1               0x10
#define ED_CR_RD2               0x20
#define ED_CR_PS0               0x40
#define ED_CR_PS1               0x80

#define ED_ISR_PRX              0x01
#define ED_ISR_PTX              0x02
#define ED_ISR_RXE              0x04
#define ED_ISR_TXE              0x08
#define ED_ISR_OVW              0x10
#define ED_ISR_CNT              0x20
#define ED_ISR_RDC              0x40
#define ED_ISR_RST              0x80

#define ED_RCR_SEP              0x01
#define ED_RCR_AB               0x04
#define ED_RCR_MON              0x20
#define ED_TCR_LB0              0x02
#define ED_DCR_LS               0x08
#define ED_DCR_FT1              0x40

#define NE_TX_PAGE              0x40
#define NE_RX_START             0x46
#define NE_RX_STOP              0x80
#define NE_PAGE_SIZE            256
#define NE_TX_MAX               1600

struct ne_ring {
    unsigned char status;
    unsigned char next;
    unsigned char count_lo;
    unsigned char count_hi;
};

struct ne_softc {
    struct arpcom sc_ac;
#define sc_if sc_ac.ac_if
    int sc_present;
    int sc_tx_busy;
    unsigned char sc_nextpkt;
    unsigned char sc_txbuf[NE_TX_MAX];
};

static struct ne_softc ne_softc[1];

static int neoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst);
static int neioctl(struct ifnet *ifp, int cmd, caddr_t data);
static int neinit(int unit);
static int nestart(int unit);
static int newatchdog(int unit);
static void nepoll(int unit);
static void nerecv(struct ne_softc *sc);
static struct mbuf *neget(struct ifnet *ifp, unsigned char *buf, int len);

static unsigned
ne_read(unsigned off)
{
    return NE_IO_ADDR[off] & 0xff;
}

static void
ne_write(unsigned off, unsigned val)
{
    NE_IO_ADDR[off] = val & 0xff;
}

static void
ne_select_page(unsigned page)
{
    ne_write(ED_P0_CR, ED_CR_STA | ED_CR_RD2 |
        (page == 1 ? ED_CR_PS0 : page == 2 ? ED_CR_PS1 : 0));
}

static void
ne_remote_start(unsigned addr, unsigned count, int write)
{
    ne_select_page(0);
    ne_write(ED_P0_ISR, ED_ISR_RDC);
    ne_write(ED_P0_RBCR0, count & 0xff);
    ne_write(ED_P0_RBCR1, count >> 8);
    ne_write(ED_P0_RSAR0, addr & 0xff);
    ne_write(ED_P0_RSAR1, addr >> 8);
    ne_write(ED_P0_CR, ED_CR_STA | (write ? ED_CR_RD1 : ED_CR_RD0));
}

static void
ne_remote_read(unsigned addr, unsigned char *buf, unsigned count)
{
    unsigned i;

    ne_remote_start(addr, count, 0);
    for (i = 0; i < count; i++)
        buf[i] = ne_read(NE_DATA);
    while ((ne_read(ED_P0_ISR) & ED_ISR_RDC) == 0)
        ;
    ne_write(ED_P0_ISR, ED_ISR_RDC);
}

static void
ne_remote_write(unsigned addr, const unsigned char *buf, unsigned count)
{
    unsigned i;

    ne_remote_start(addr, count, 1);
    for (i = 0; i < count; i++)
        ne_write(NE_DATA, buf[i]);
    while ((ne_read(ED_P0_ISR) & ED_ISR_RDC) == 0)
        ;
    ne_write(ED_P0_ISR, ED_ISR_RDC);
}

static int
ne_probe_mac(unsigned char *enaddr)
{
    unsigned char prom[32];
    int i;

    ne_write(ED_P0_CR, ED_CR_STP | ED_CR_RD2);
    ne_write(ED_P0_DCR, ED_DCR_LS | ED_DCR_FT1);
    ne_write(ED_P0_RBCR0, 0);
    ne_write(ED_P0_RBCR1, 0);
    ne_write(ED_P0_RCR, ED_RCR_MON);
    ne_write(ED_P0_TCR, ED_TCR_LB0);
    ne_write(ED_P0_PSTART, NE_RX_START);
    ne_write(ED_P0_PSTOP, NE_RX_STOP);
    ne_write(ED_P0_BNRY, NE_RX_START);
    ne_write(ED_P0_ISR, 0xff);
    ne_write(ED_P0_IMR, 0);

    ne_remote_read(0, prom, sizeof(prom));
    for (i = 0; i < 6; i++)
        enaddr[i] = prom[i * 2];

    if ((enaddr[0] == 0x00 && enaddr[1] == 0x00 && enaddr[2] == 0x00) ||
        (enaddr[0] == 0xff && enaddr[1] == 0xff && enaddr[2] == 0xff))
        return 0;
    return 1;
}

void
malta_neattach(int unit)
{
    struct ne_softc *sc = &ne_softc[0];
    struct ifnet *ifp = &sc->sc_if;
    unsigned char reset;

    if (unit != 0)
        return;

    reset = ne_read(NE_RESET);
    ne_write(NE_RESET, reset);
    while ((ne_read(ED_P0_ISR) & ED_ISR_RST) == 0)
        ;
    ne_write(ED_P0_ISR, ED_ISR_RST);

    if (!ne_probe_mac(sc->sc_ac.ac_enaddr)) {
        printf("ne0: not found at isa 0x%x\n", NE_IO_BASE);
        return;
    }

    sc->sc_present = 1;
    sc->sc_nextpkt = NE_RX_START + 1;
    ifp->if_name = "ne";
    ifp->if_unit = 0;
    ifp->if_mtu = ETHERMTU;
    ifp->if_flags = IFF_BROADCAST | IFF_NOTRAILERS;
    ifp->if_init = neinit;
    ifp->if_output = neoutput;
    ifp->if_ioctl = neioctl;
    ifp->if_watchdog = newatchdog;
    if_attach(ifp);
    printf("ne0: qemu ne2k isa 0x%x address %s\n",
        NE_IO_BASE, ether_sprintf(sc->sc_ac.ac_enaddr));
}

static int
neinit(int unit)
{
    struct ne_softc *sc = &ne_softc[unit];
    int i;

    if (!sc->sc_present)
        return 0;

    ne_write(ED_P0_CR, ED_CR_STP | ED_CR_RD2);
    ne_write(ED_P0_DCR, ED_DCR_LS | ED_DCR_FT1);
    ne_write(ED_P0_RBCR0, 0);
    ne_write(ED_P0_RBCR1, 0);
    ne_write(ED_P0_RCR, ED_RCR_MON);
    ne_write(ED_P0_TCR, ED_TCR_LB0);
    ne_write(ED_P0_TPSR, NE_TX_PAGE);
    ne_write(ED_P0_PSTART, NE_RX_START);
    ne_write(ED_P0_PSTOP, NE_RX_STOP);
    ne_write(ED_P0_BNRY, NE_RX_START);
    ne_write(ED_P0_ISR, 0xff);
    ne_write(ED_P0_IMR, 0);

    ne_select_page(1);
    for (i = 0; i < 6; i++)
        ne_write(ED_P1_PAR0 + i, sc->sc_ac.ac_enaddr[i]);
    for (i = 0; i < 8; i++)
        ne_write(ED_P1_MAR0 + i, 0);
    ne_write(ED_P1_CURR, NE_RX_START + 1);
    ne_select_page(0);

    sc->sc_nextpkt = NE_RX_START + 1;
    sc->sc_tx_busy = 0;
    ne_write(ED_P0_CR, ED_CR_STA | ED_CR_RD2);
    ne_write(ED_P0_RCR, ED_RCR_AB);
    ne_write(ED_P0_TCR, 0);
    sc->sc_if.if_flags |= IFF_RUNNING;
    nestart(unit);
    return 0;
}

static int
neioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
    struct ne_softc *sc = &ne_softc[ifp->if_unit];
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
        neinit(ifp->if_unit);
        break;

    case SIOCSIFFLAGS:
        if ((ifp->if_flags & IFF_UP) == 0)
            ifp->if_flags &= ~IFF_RUNNING;
        else if ((ifp->if_flags & IFF_RUNNING) == 0)
            neinit(ifp->if_unit);
        break;

    default:
        error = EINVAL;
        break;
    }
    splx(s);
    return error;
}

static int
neoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst)
{
    struct ne_softc *sc = &ne_softc[ifp->if_unit];
    struct mbuf *m = m0;
    struct ether_header *eh;
    struct in_addr idst;
    unsigned char edst[6];
    int error, off, s, type, usetrailers;

    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) !=
        (IFF_UP | IFF_RUNNING)) {
        error = ENETDOWN;
        goto bad;
    }

    switch (dst->sa_family) {
    case AF_INET:
        idst = ((struct sockaddr_in *)dst)->sin_addr;
        if (!arpresolve(&sc->sc_ac, m, &idst, edst, &usetrailers))
            return 0;
        off = ntohs((u_short)mtod(m, struct ip *)->ip_len) - m->m_len;
        type = ETHERTYPE_IP;
        break;

    case AF_UNSPEC:
        eh = (struct ether_header *)dst->sa_data;
        bcopy((caddr_t)eh->ether_dhost, (caddr_t)edst, sizeof(edst));
        type = eh->ether_type;
        off = 0;
        break;

    default:
        printf("ne%d: can't handle af%d\n", ifp->if_unit, dst->sa_family);
        error = EAFNOSUPPORT;
        goto bad;
    }
    (void)off;

    if (m->m_off > MMAXOFF ||
        MMINOFF + sizeof(struct ether_header) > m->m_off) {
        m = m_get(M_DONTWAIT, MT_HEADER);
        if (m == 0) {
            error = ENOBUFS;
            goto bad;
        }
        m->m_next = m0;
        m->m_off = MMINOFF;
        m->m_len = sizeof(struct ether_header);
    } else {
        m->m_off -= sizeof(struct ether_header);
        m->m_len += sizeof(struct ether_header);
    }

    eh = mtod(m, struct ether_header *);
    eh->ether_type = htons((u_short)type);
    bcopy((caddr_t)edst, (caddr_t)eh->ether_dhost, sizeof(edst));
    bcopy((caddr_t)sc->sc_ac.ac_enaddr, (caddr_t)eh->ether_shost,
        sizeof(sc->sc_ac.ac_enaddr));

    s = splimp();
    if (IF_QFULL(&ifp->if_snd)) {
        IF_DROP(&ifp->if_snd);
        splx(s);
        m_freem(m);
        return ENOBUFS;
    }
    IF_ENQUEUE(&ifp->if_snd, m);
    nestart(ifp->if_unit);
    splx(s);
    return 0;

bad:
    m_freem(m0);
    return error;
}

static int
nestart(int unit)
{
    struct ne_softc *sc = &ne_softc[unit];
    struct mbuf *m, *n;
    unsigned count = 0;
    unsigned mincount;
    int len;

    if (sc->sc_tx_busy)
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
    ne_remote_write(NE_TX_PAGE * NE_PAGE_SIZE, sc->sc_txbuf, count);
    ne_write(ED_P0_TPSR, NE_TX_PAGE);
    ne_write(ED_P0_TBCR0, count & 0xff);
    ne_write(ED_P0_TBCR1, count >> 8);
    ne_write(ED_P0_CR, ED_CR_STA | ED_CR_TXP | ED_CR_RD2);
    sc->sc_if.if_timer = 2;
    return 0;
}

static int
newatchdog(int unit)
{
    struct ne_softc *sc = &ne_softc[unit];

    nepoll(unit);
    if (sc->sc_tx_busy) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_oerrors++;
        nestart(unit);
    }
    return 0;
}

static void
nepoll(int unit)
{
    struct ne_softc *sc = &ne_softc[unit];
    unsigned isr;

    if (!sc->sc_present || (sc->sc_if.if_flags & IFF_RUNNING) == 0)
        return;
    isr = ne_read(ED_P0_ISR);
    if (isr)
        ne_write(ED_P0_ISR, isr);
    if (isr & (ED_ISR_PTX | ED_ISR_TXE)) {
        if (isr & ED_ISR_TXE)
            sc->sc_if.if_oerrors++;
        else
            sc->sc_if.if_opackets++;
        sc->sc_tx_busy = 0;
        sc->sc_if.if_timer = 0;
        nestart(unit);
    }
    nerecv(sc);
}

void
malta_nepoll(void)
{
    nepoll(0);
}

static struct mbuf *
neget(struct ifnet *ifp, unsigned char *buf, int len)
{
    struct mbuf *top, **mp, *m;
    int n;

    top = 0;
    mp = &top;
    while (len > 0) {
        MGET(m, M_DONTWAIT, MT_DATA);
        if (m == 0)
            goto bad;
        m->m_off = MMINOFF;
        if (ifp) {
            m->m_len = MIN(MLEN - sizeof(struct ifnet *), len);
            m->m_off += sizeof(struct ifnet *);
        } else
            m->m_len = MIN(MLEN, len);
        n = m->m_len;
        bcopy((caddr_t)buf, mtod(m, caddr_t), n);
        buf += n;
        len -= n;
        *mp = m;
        mp = &m->m_next;
        if (ifp) {
            m->m_len += sizeof(struct ifnet *);
            m->m_off -= sizeof(struct ifnet *);
            *(mtod(m, struct ifnet **)) = ifp;
            ifp = 0;
        }
    }
    return top;

bad:
    m_freem(top);
    return 0;
}

static void
nerecv(struct ne_softc *sc)
{
    struct ne_ring hdr;
    struct ether_header *eh;
    struct ifqueue *inq;
    struct mbuf *m;
    unsigned char frame[1600];
    unsigned char curr, next, bnry;
    unsigned addr, count, len, first;
    int s, type;

    for (;;) {
        ne_select_page(1);
        curr = ne_read(ED_P1_CURR);
        ne_select_page(0);
        if (sc->sc_nextpkt == curr)
            break;

        addr = sc->sc_nextpkt * NE_PAGE_SIZE;
        ne_remote_read(addr, (unsigned char *)&hdr, sizeof(hdr));
        count = hdr.count_lo | (hdr.count_hi << 8);
        next = hdr.next;
        if (next < NE_RX_START || next >= NE_RX_STOP ||
            count < sizeof(struct ether_header) + sizeof(hdr) ||
            count > sizeof(frame) + sizeof(hdr)) {
            sc->sc_if.if_ierrors++;
            sc->sc_nextpkt = NE_RX_START + 1;
            ne_select_page(1);
            ne_write(ED_P1_CURR, sc->sc_nextpkt);
            ne_select_page(0);
            ne_write(ED_P0_BNRY, NE_RX_START);
            break;
        }

        len = count - sizeof(hdr);
        first = (NE_RX_STOP - sc->sc_nextpkt) * NE_PAGE_SIZE -
            sizeof(hdr);
        if (first >= len)
            ne_remote_read(addr + sizeof(hdr), frame, len);
        else {
            ne_remote_read(addr + sizeof(hdr), frame, first);
            ne_remote_read(NE_RX_START * NE_PAGE_SIZE, frame + first,
                len - first);
        }

        sc->sc_nextpkt = next;
        bnry = next == NE_RX_START ? NE_RX_STOP - 1 : next - 1;
        ne_write(ED_P0_BNRY, bnry);

        if ((hdr.status & 0x01) == 0) {
            sc->sc_if.if_ierrors++;
            continue;
        }
        if (len < sizeof(struct ether_header)) {
            sc->sc_if.if_ierrors++;
            continue;
        }

        eh = (struct ether_header *)frame;
        type = ntohs((u_short)eh->ether_type);
        len -= sizeof(struct ether_header);
        m = neget(&sc->sc_if, frame + sizeof(struct ether_header), len);
        if (m == 0) {
            sc->sc_if.if_ierrors++;
            continue;
        }

        switch (type) {
        case ETHERTYPE_IP:
            schednetisr(NETISR_IP);
            inq = &ipintrq;
            break;

        case ETHERTYPE_ARP:
            arpinput(&sc->sc_ac, m);
            sc->sc_if.if_ipackets++;
            continue;

        default:
            m_freem(m);
            continue;
        }

        s = splimp();
        if (IF_QFULL(inq)) {
            IF_DROP(inq);
            splx(s);
            m_freem(m);
            continue;
        }
        IF_ENQUEUE(inq, m);
        sc->sc_if.if_ipackets++;
        splx(s);
    }
}
