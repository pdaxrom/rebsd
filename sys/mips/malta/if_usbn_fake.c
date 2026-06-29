/*
 * QEMU Malta fake USB Ethernet transport.
 *
 * This lower half lets the generic if_usbn upper half run through the normal
 * ARP/IP output path without N64 USB hardware.  It emulates one peer:
 *
 *      usbn0        10.64.0.2  02:64:00:00:00:01
 *      fake peer    10.64.0.1  02:64:00:00:00:02
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>

#include <mips/common/if_usbn.h>

#define USBN_FAKE_FRAME_MAX     1600
#define USBN_FAKE_PEER_IP       0x0a400001UL
#define USBN_FAKE_ETH_HDR       14

#define ETH_DST                 0
#define ETH_SRC                 6
#define ETH_TYPE                12

#define ARP_HRD                 0
#define ARP_PRO                 2
#define ARP_HLN                 4
#define ARP_PLN                 5
#define ARP_OP                  6
#define ARP_SHA                 8
#define ARP_SPA                 14
#define ARP_THA                 18
#define ARP_TPA                 24
#define ARP_LEN                 28

#define IP_VHL                  0
#define IP_LEN                  2
#define IP_TTL                  8
#define IP_PROTO                9
#define IP_SUM                  10
#define IP_SRC                  12
#define IP_DST                  16

static unsigned char usbn_fake_local_mac[6] =
    { 0x02, 0x64, 0x00, 0x00, 0x00, 0x01 };
static unsigned char usbn_fake_peer_mac[6] =
    { 0x02, 0x64, 0x00, 0x00, 0x00, 0x02 };
static unsigned char usbn_fake_rxbuf[USBN_FAKE_FRAME_MAX];

static unsigned usbn_fake_get16(const unsigned char *p);
static unsigned long usbn_fake_get32(const unsigned char *p);
static void usbn_fake_put16(unsigned char *p, unsigned val);
static int usbn_fake_is_peer_ip(const unsigned char *addr);
static unsigned short usbn_fake_cksum(const unsigned char *buf, unsigned len);
static void usbn_fake_arp_reply(const unsigned char *frame, unsigned len);
static void usbn_fake_icmp_reply(const unsigned char *frame, unsigned len);

int
usbn_hw_init(int unit, unsigned char *enaddr)
{
    if (unit != 0)
        return 0;
    bcopy((caddr_t)usbn_fake_local_mac, (caddr_t)enaddr,
        sizeof(usbn_fake_local_mac));
    return 1;
}

int
usbn_hw_send(int unit, const unsigned char *frame, unsigned len)
{
    unsigned type;

    if (unit != 0 || len < USBN_FAKE_ETH_HDR) {
        usbn_tx_done(unit, 1);
        return 0;
    }

    type = usbn_fake_get16(frame + ETH_TYPE);
    if (type == ETHERTYPE_ARP)
        usbn_fake_arp_reply(frame, len);
    else if (type == ETHERTYPE_IP)
        usbn_fake_icmp_reply(frame, len);

    usbn_tx_done(unit, 0);
    return 0;
}

void
usbn_hw_poll(void)
{
}

static unsigned
usbn_fake_get16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long
usbn_fake_get32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) |
        ((unsigned long)p[1] << 16) |
        ((unsigned long)p[2] << 8) |
        (unsigned long)p[3];
}

static void
usbn_fake_put16(unsigned char *p, unsigned val)
{
    p[0] = (val >> 8) & 0xff;
    p[1] = val & 0xff;
}

static int
usbn_fake_is_peer_ip(const unsigned char *addr)
{
    return usbn_fake_get32(addr) == USBN_FAKE_PEER_IP;
}

static unsigned short
usbn_fake_cksum(const unsigned char *buf, unsigned len)
{
    unsigned long sum = 0;

    while (len > 1) {
        sum += ((unsigned)buf[0] << 8) | buf[1];
        buf += 2;
        len -= 2;
    }
    if (len)
        sum += (unsigned)buf[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}

static void
usbn_fake_arp_reply(const unsigned char *frame, unsigned len)
{
    const unsigned char *arp;
    unsigned char *rep;
    unsigned rlen;

    if (len < USBN_FAKE_ETH_HDR + ARP_LEN)
        return;
    arp = frame + USBN_FAKE_ETH_HDR;
    if (usbn_fake_get16(arp + ARP_HRD) != ARPHRD_ETHER ||
        usbn_fake_get16(arp + ARP_PRO) != ETHERTYPE_IP ||
        arp[ARP_HLN] != 6 || arp[ARP_PLN] != 4 ||
        usbn_fake_get16(arp + ARP_OP) != ARPOP_REQUEST ||
        !usbn_fake_is_peer_ip(arp + ARP_TPA))
        return;

    rlen = USBN_FAKE_ETH_HDR + ARP_LEN;
    bzero((caddr_t)usbn_fake_rxbuf, sizeof(usbn_fake_rxbuf));
    rep = usbn_fake_rxbuf + USBN_FAKE_ETH_HDR;

    bcopy((caddr_t)(frame + ETH_SRC), (caddr_t)(usbn_fake_rxbuf + ETH_DST),
        6);
    bcopy((caddr_t)usbn_fake_peer_mac, (caddr_t)(usbn_fake_rxbuf + ETH_SRC),
        6);
    usbn_fake_put16(usbn_fake_rxbuf + ETH_TYPE, ETHERTYPE_ARP);

    usbn_fake_put16(rep + ARP_HRD, ARPHRD_ETHER);
    usbn_fake_put16(rep + ARP_PRO, ETHERTYPE_IP);
    rep[ARP_HLN] = 6;
    rep[ARP_PLN] = 4;
    usbn_fake_put16(rep + ARP_OP, ARPOP_REPLY);
    bcopy((caddr_t)usbn_fake_peer_mac, (caddr_t)(rep + ARP_SHA), 6);
    bcopy((caddr_t)(arp + ARP_TPA), (caddr_t)(rep + ARP_SPA), 4);
    bcopy((caddr_t)(arp + ARP_SHA), (caddr_t)(rep + ARP_THA), 6);
    bcopy((caddr_t)(arp + ARP_SPA), (caddr_t)(rep + ARP_TPA), 4);

    usbn_input(0, usbn_fake_rxbuf, rlen);
}

static void
usbn_fake_icmp_reply(const unsigned char *frame, unsigned len)
{
    const unsigned char *ip;
    unsigned char *rep_ip, *rep_icmp;
    unsigned iplen, ihl, icmplen, rlen;
    unsigned char src[4];

    if (len < USBN_FAKE_ETH_HDR + sizeof(struct ip) + ICMP_MINLEN)
        return;
    ip = frame + USBN_FAKE_ETH_HDR;
    if ((ip[IP_VHL] >> 4) != IPVERSION ||
        (ip[IP_VHL] & 0x0f) < 5 ||
        ip[IP_PROTO] != IPPROTO_ICMP)
        return;
    ihl = (ip[IP_VHL] & 0x0f) << 2;
    iplen = usbn_fake_get16(ip + IP_LEN);
    if (iplen < ihl + ICMP_MINLEN ||
        USBN_FAKE_ETH_HDR + iplen > len ||
        USBN_FAKE_ETH_HDR + iplen > sizeof(usbn_fake_rxbuf))
        return;
    if (usbn_fake_get32(ip + IP_DST) != USBN_FAKE_PEER_IP)
        return;
    if (ip[ihl] != ICMP_ECHO)
        return;

    rlen = USBN_FAKE_ETH_HDR + iplen;
    bcopy((caddr_t)frame, (caddr_t)usbn_fake_rxbuf, rlen);

    bcopy((caddr_t)(frame + ETH_SRC), (caddr_t)(usbn_fake_rxbuf + ETH_DST),
        6);
    bcopy((caddr_t)usbn_fake_peer_mac, (caddr_t)(usbn_fake_rxbuf + ETH_SRC),
        6);

    rep_ip = usbn_fake_rxbuf + USBN_FAKE_ETH_HDR;
    bcopy((caddr_t)(rep_ip + IP_SRC), (caddr_t)src, sizeof(src));
    bcopy((caddr_t)(rep_ip + IP_DST), (caddr_t)(rep_ip + IP_SRC), 4);
    bcopy((caddr_t)src, (caddr_t)(rep_ip + IP_DST), 4);
    rep_ip[IP_TTL] = 255;
    rep_ip[IP_SUM] = 0;
    rep_ip[IP_SUM + 1] = 0;
    usbn_fake_put16(rep_ip + IP_SUM, usbn_fake_cksum(rep_ip, ihl));

    rep_icmp = rep_ip + ihl;
    icmplen = iplen - ihl;
    rep_icmp[0] = ICMP_ECHOREPLY;
    rep_icmp[1] = 0;
    rep_icmp[2] = 0;
    rep_icmp[3] = 0;
    usbn_fake_put16(rep_icmp + 2, usbn_fake_cksum(rep_icmp, icmplen));

    usbn_input(0, usbn_fake_rxbuf, rlen);
}
