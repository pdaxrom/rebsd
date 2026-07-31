/*
 * BSD ifnet front-end for the machine-independent RTL8169 PCI driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/domain.h>
#include <sys/protosw.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>

#include <pci/pci.h>
#include <pci/rtl8169.h>

#define RE_NUNITS                       1
#define RE_MIN_FRAME_BYTES              60u

struct re_softc {
    struct arpcom sc_ac;
#define sc_if sc_ac.ac_if
    struct rtl8169_softc sc_rtl;
    unsigned sc_present;
    int sc_link;
    unsigned char sc_tx_buffer[RTL8169_FRAME_BYTES];
};

static struct re_softc re_softc[RE_NUNITS];

static int reoutput(struct ifnet *, struct mbuf *, struct sockaddr *);
static int reioctl(struct ifnet *, int, caddr_t);
static int reinit(int);
static int restart(int);
static int rewatchdog(int);

static void
re_receive(void *arg, const unsigned char *frame, unsigned length)
{
    struct re_softc *sc;

    sc = (struct re_softc *)arg;
    (void)ether_input_frame(&sc->sc_ac, frame, length);
}

static void
re_receive_error(void *arg)
{
    struct re_softc *sc;

    sc = (struct re_softc *)arg;
    sc->sc_if.if_ierrors++;
}

static void
re_transmit_done(void *arg, unsigned completed, unsigned errors)
{
    struct re_softc *sc;

    sc = (struct re_softc *)arg;
    sc->sc_if.if_opackets += completed - errors;
    sc->sc_if.if_oerrors += errors;
    sc->sc_if.if_timer = sc->sc_rtl.rs_tx_used != 0 ? 2 : 0;
    (void)restart(sc->sc_if.if_unit);
}

static void
re_link_change(void *arg, int link)
{
    struct re_softc *sc;

    sc = (struct re_softc *)arg;
    if (sc->sc_link == link)
        return;
    sc->sc_link = link;
    printf("re%d: link %s\n", sc->sc_if.if_unit,
        link ? "up" : "down");
}

static const struct rtl8169_callbacks re_callbacks = {
    re_receive,
    re_receive_error,
    re_transmit_done,
    re_link_change,
};

void
rtl8169attach(int unit)
{
    struct pci_bus *bus;
    struct pci_device device;
    struct re_softc *sc;
    struct ifnet *ifp;
    int error;

    if (unit < 0 || unit >= RE_NUNITS)
        return;
    sc = &re_softc[unit];
    if (sc->sc_present)
        return;
    bus = pci_primary_bus();
    if (bus == 0 || !pci_find_device(bus, RTL8169_VENDOR_REALTEK,
        RTL8169_PRODUCT_8169, &device))
        return;
    error = rtl8169_attach(&sc->sc_rtl, &device, &re_callbacks, sc);
    if (error != 0) {
        printf("re%d: RTL8169 attach failed, error=%d xid=%x\n",
            unit, error, sc->sc_rtl.rs_xid);
        return;
    }

    bcopy((caddr_t)sc->sc_rtl.rs_enaddr,
        (caddr_t)sc->sc_ac.ac_enaddr, sizeof(sc->sc_ac.ac_enaddr));
    sc->sc_present = 1;
    sc->sc_link = -1;
    ifp = &sc->sc_if;
    ifp->if_name = "re";
    ifp->if_unit = unit;
    ifp->if_mtu = ETHERMTU;
    ifp->if_flags = IFF_BROADCAST | IFF_NOTRAILERS;
    ifp->if_init = reinit;
    ifp->if_output = reoutput;
    ifp->if_ioctl = reioctl;
    ifp->if_watchdog = rewatchdog;
    if_attach(ifp);
    printf("re%d: RTL8169 mac-ver=%u xid=%x %s BAR%u=%x irq=%u\n",
        unit, sc->sc_rtl.rs_mac_version, sc->sc_rtl.rs_xid,
        sc->sc_rtl.rs_registers.pr_type == PCI_RESOURCE_MEMORY ?
        "memory" : "io", sc->sc_rtl.rs_registers.pr_bar,
        (unsigned)sc->sc_rtl.rs_registers.pr_address,
        pci_config_read8(&device, PCI_CONFIG_INTERRUPT));
    printf("re%d: address %s\n", unit,
        ether_sprintf(sc->sc_ac.ac_enaddr));
}

static int
reinit(int unit)
{
    struct re_softc *sc;
    int error;

    if (unit < 0 || unit >= RE_NUNITS)
        return ENXIO;
    sc = &re_softc[unit];
    if (!sc->sc_present)
        return ENXIO;
    error = rtl8169_start(&sc->sc_rtl);
    if (error != 0) {
        printf("re%d: start failed, error=%d\n", unit, error);
        sc->sc_if.if_flags &= ~IFF_RUNNING;
        return error;
    }
    sc->sc_if.if_flags |= IFF_RUNNING;
    (void)restart(unit);
    return 0;
}

static int
reioctl(struct ifnet *ifp, int cmd, caddr_t data)
{
    struct re_softc *sc;
    struct in_ifaddr *ia;
    int error;
    int s;

    sc = &re_softc[ifp->if_unit];
    ia = (struct in_ifaddr *)data;
    error = 0;
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
        error = reinit(ifp->if_unit);
        break;

    case SIOCSIFFLAGS:
        if ((ifp->if_flags & IFF_UP) == 0) {
            rtl8169_stop(&sc->sc_rtl);
            ifp->if_flags &= ~IFF_RUNNING;
        } else if ((ifp->if_flags & IFF_RUNNING) == 0)
            error = reinit(ifp->if_unit);
        break;

    default:
        error = EINVAL;
        break;
    }
    splx(s);
    return error;
}

static int
reoutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst)
{
    struct re_softc *sc;

    sc = &re_softc[ifp->if_unit];
    return ether_output_enqueue(&sc->sc_ac, m, dst, restart);
}

static int
restart(int unit)
{
    struct re_softc *sc;
    struct mbuf *m;
    struct mbuf *next;
    unsigned count;
    unsigned copy;
    int oversized;
    int error;

    if (unit < 0 || unit >= RE_NUNITS)
        return ENXIO;
    sc = &re_softc[unit];
    while (rtl8169_tx_available(&sc->sc_rtl) != 0) {
        IF_DEQUEUE(&sc->sc_if.if_snd, m);
        if (m == 0)
            break;
        count = 0;
        oversized = 0;
        while (m != 0) {
            copy = m->m_len;
            if (copy > sizeof(sc->sc_tx_buffer) - count) {
                copy = sizeof(sc->sc_tx_buffer) - count;
                oversized = 1;
            }
            if (copy != 0) {
                bcopy(mtod(m, caddr_t),
                    (caddr_t)&sc->sc_tx_buffer[count], copy);
                count += copy;
            }
            MFREE(m, next);
            m = next;
        }
        if (oversized) {
            sc->sc_if.if_oerrors++;
            continue;
        }
        if (count < RE_MIN_FRAME_BYTES) {
            bzero((caddr_t)&sc->sc_tx_buffer[count],
                RE_MIN_FRAME_BYTES - count);
            count = RE_MIN_FRAME_BYTES;
        }
        error = rtl8169_transmit(&sc->sc_rtl, sc->sc_tx_buffer, count);
        if (error != 0) {
            sc->sc_if.if_oerrors++;
            if (error == EBUSY)
                break;
        }
    }
    sc->sc_if.if_timer = sc->sc_rtl.rs_tx_used != 0 ? 2 : 0;
    return 0;
}

static int
rewatchdog(int unit)
{
    struct re_softc *sc;
    unsigned outstanding;
    int error;

    if (unit < 0 || unit >= RE_NUNITS)
        return ENXIO;
    sc = &re_softc[unit];
    outstanding = sc->sc_rtl.rs_tx_used;
    if (outstanding == 0)
        return 0;
    sc->sc_if.if_oerrors += outstanding;
    rtl8169_stop(&sc->sc_rtl);
    error = rtl8169_start(&sc->sc_rtl);
    if (error == 0) {
        sc->sc_if.if_flags |= IFF_RUNNING;
        (void)restart(unit);
    } else
        sc->sc_if.if_flags &= ~IFF_RUNNING;
    return error;
}
