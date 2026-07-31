#include <sys/param.h>

#ifdef INET
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

void mbinit(void);
void ifinit(void);
void loattach(void);
void domaininit(void);
void ipintr(void);
void rawintr(void);
int in_control();

extern struct ifnet loif;

#include <net/netisr.h>

int netoff;
int netisr;

static void
loopback_init(void)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int error;

    bzero((caddr_t)&ifr, sizeof(ifr));
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(((u_long)IN_LOOPBACKNET << 24) | 1);
    error = in_control((struct socket *)0, SIOCSIFADDR, (caddr_t)&ifr,
        &loif);
    if (error)
        printf("netinit: lo0 address error=%d\n", error);
}

void
inetattach(int unit)
{
    (void)unit;
    mbinit();
    loattach();
    ifinit();
    domaininit();
    loopback_init();
    netoff = 0;
}

void
netintr(void)
{
    int s;
    int pending;

    for (;;) {
        s = splnet();
        pending = netisr;
        netisr = 0;
        splx(s);

        if (pending == 0)
            break;
        if (pending & (1 << NETISR_IP))
            ipintr();
        if (pending & (1 << NETISR_RAW))
            rawintr();
    }
}
#endif
