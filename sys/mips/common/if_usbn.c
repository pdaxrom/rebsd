#include <sys/param.h>

/*
 * Generic USB Ethernet-like interface upper half.
 *
 * The board-specific lower half moves complete Ethernet frames over a transport
 * such as the N64cart USB device endpoints.  This file only handles BSD ifnet,
 * ARP, queues, and mbuf handoff.
 */
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
usbnetattach(void)
{
    /*
     * kconfig services are invoked through conf_service_init without
     * arguments.  Do not consume the caller's stale a0 register as a unit:
     * it made attachment depend on which service happened to run before us.
     */
    usbnattach(0);
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

    return ether_output_enqueue(&sc->sc_ac, m0, dst, usbnstart);
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

void
usbn_input(int unit, const unsigned char *frame, unsigned len)
{
    struct usbn_softc *sc;

    if (unit < 0 || unit >= USBN_NUNITS)
        return;
    sc = &usbn_softc[unit];
    if (!sc->sc_present || (sc->sc_if.if_flags & IFF_RUNNING) == 0)
        return;
    (void)ether_input_frame(&sc->sc_ac, frame, len);
}
