/*
 * Generic USB Ethernet-like interface upper half.
 *
 * The board-specific lower half moves complete Ethernet frames over a transport
 * such as the N64cart USB device endpoints.  This file only handles BSD ifnet,
 * ARP, queues, and mbuf handoff.
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

#include <mips/common/if_usbn.h>

#define USBN_NUNITS             1
#define USBN_TX_MAX             1600

struct usbn_softc {
    struct arpcom sc_ac;
#define sc_if sc_ac.ac_if
    int sc_present;
    int sc_tx_busy;
    unsigned char sc_txbuf[USBN_TX_MAX];
};

static struct usbn_softc usbn_softc[USBN_NUNITS];

static int usbnoutput(struct ifnet *ifp, struct mbuf *m0,
    struct sockaddr *dst);
static int usbnioctl(struct ifnet *ifp, int cmd, caddr_t data);
static int usbninit(int unit);
static int usbnstart(int unit);
static int usbnwatchdog(int unit);
static struct mbuf *usbnget(struct ifnet *ifp, const unsigned char *buf,
    int len);

void
usbnattach(int unit)
{
    struct usbn_softc *sc;
    struct ifnet *ifp;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    ifp = &sc->sc_if;
    if (!usbn_hw_init(unit, sc->sc_ac.ac_enaddr)) {
        printf("usbn%d: not found\n", unit);
        return;
    }

    sc->sc_present = 1;
    ifp->if_name = "usbn";
    ifp->if_unit = unit;
    ifp->if_mtu = ETHERMTU;
    ifp->if_flags = IFF_BROADCAST | IFF_NOTRAILERS;
    ifp->if_init = usbninit;
    ifp->if_output = usbnoutput;
    ifp->if_ioctl = usbnioctl;
    ifp->if_watchdog = usbnwatchdog;
    if_attach(ifp);
    printf("usbn%d: usb ethernet address %s\n", unit,
        ether_sprintf(sc->sc_ac.ac_enaddr));
}

void
usbnetattach(int unit)
{
    usbnattach(unit);
}

void
usbnpoll(void)
{
    usbn_hw_poll();
}

static int
usbninit(int unit)
{
    struct usbn_softc *sc = &usbn_softc[unit];

    if (!sc->sc_present)
        return 0;
    sc->sc_tx_busy = 0;
    sc->sc_if.if_flags |= IFF_RUNNING;
    usbnstart(unit);
    return 0;
}

static int
usbnioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
    struct usbn_softc *sc = &usbn_softc[ifp->if_unit];
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
        usbninit(ifp->if_unit);
        break;

    case SIOCSIFFLAGS:
        if ((ifp->if_flags & IFF_UP) == 0)
            ifp->if_flags &= ~IFF_RUNNING;
        else if ((ifp->if_flags & IFF_RUNNING) == 0)
            usbninit(ifp->if_unit);
        break;

    default:
        error = EINVAL;
        break;
    }
    splx(s);
    return error;
}

static int
usbnoutput(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst)
{
    struct usbn_softc *sc = &usbn_softc[ifp->if_unit];
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
        printf("usbn%d: can't handle af%d\n", ifp->if_unit,
            dst->sa_family);
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
    usbnstart(ifp->if_unit);
    splx(s);
    return 0;

bad:
    m_freem(m0);
    return error;
}

static int
usbnstart(int unit)
{
    struct usbn_softc *sc = &usbn_softc[unit];
    struct mbuf *m, *n;
    unsigned count = 0;
    unsigned mincount;
    int len, error;

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
    sc->sc_if.if_timer = 2;
    error = usbn_hw_send(unit, sc->sc_txbuf, count);
    if (error) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_timer = 0;
        sc->sc_if.if_oerrors++;
        usbnstart(unit);
        return error;
    }
    return 0;
}

static int
usbnwatchdog(int unit)
{
    struct usbn_softc *sc = &usbn_softc[unit];

    usbnpoll();
    if (sc->sc_tx_busy) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_oerrors++;
        usbnstart(unit);
    }
    return 0;
}

void
usbn_input_error(int unit)
{
    struct usbn_softc *sc;
    int s;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    if (!sc->sc_present)
        return;
    s = splimp();
    sc->sc_if.if_ierrors++;
    splx(s);
}

void
usbn_link_reset(int unit)
{
    struct usbn_softc *sc;
    int s;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    if (!sc->sc_present)
        return;
    s = splimp();
    if (sc->sc_tx_busy) {
        sc->sc_tx_busy = 0;
        sc->sc_if.if_timer = 0;
        sc->sc_if.if_oerrors++;
    }
    splx(s);
}

void
usbn_tx_done(int unit, int error)
{
    struct usbn_softc *sc;
    int s;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    s = splimp();
    if (sc->sc_tx_busy) {
        if (error)
            sc->sc_if.if_oerrors++;
        else
            sc->sc_if.if_opackets++;
        sc->sc_tx_busy = 0;
        sc->sc_if.if_timer = 0;
        usbnstart(unit);
    }
    splx(s);
}

static struct mbuf *
usbnget(struct ifnet *ifp, const unsigned char *buf, int len)
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

void
usbn_input(int unit, const unsigned char *frame, unsigned len)
{
    struct usbn_softc *sc;
    struct ether_header *eh;
    struct ifqueue *inq;
    struct mbuf *m;
    int s, type;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    if (!sc->sc_present || (sc->sc_if.if_flags & IFF_RUNNING) == 0)
        return;
    if (len < sizeof(struct ether_header)) {
        sc->sc_if.if_ierrors++;
        return;
    }

    eh = (struct ether_header *)frame;
    type = ntohs((u_short)eh->ether_type);
    len -= sizeof(struct ether_header);
    m = usbnget(&sc->sc_if, frame + sizeof(struct ether_header), len);
    if (m == 0) {
        sc->sc_if.if_ierrors++;
        return;
    }

    switch (type) {
    case ETHERTYPE_IP:
        schednetisr(NETISR_IP);
        inq = &ipintrq;
        break;

    case ETHERTYPE_ARP:
        arpinput(&sc->sc_ac, m);
        sc->sc_if.if_ipackets++;
        return;

    default:
        m_freem(m);
        return;
    }

    s = splimp();
    if (IF_QFULL(inq)) {
        IF_DROP(inq);
        splx(s);
        m_freem(m);
        return;
    }
    IF_ENQUEUE(inq, m);
    sc->sc_if.if_ipackets++;
    splx(s);
}
