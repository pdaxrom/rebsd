/*
 * Minimal DHCPv4 client for RetroBSD/MIPS.
 *
 * It configures an interface through the existing ifconfig(8) and route(8)
 * commands and writes resolver state to /var/run/resolv.conf.  The rootfs
 * keeps /etc/resolv.conf as a symlink to that writable file.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST 0xffffffffL
#endif

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2

#define DHCP_HTYPE_ETHER 1
#define DHCP_HLEN_ETHER  6

#define DHCP_OPT_PAD             0
#define DHCP_OPT_SUBNET_MASK     1
#define DHCP_OPT_ROUTER          3
#define DHCP_OPT_DNS             6
#define DHCP_OPT_MESSAGE_TYPE   53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_REQUESTED_IP   50
#define DHCP_OPT_PARAM_REQ      55
#define DHCP_OPT_CLIENT_ID      61
#define DHCP_OPT_END           255

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

struct dhcp_packet {
    unsigned char op;
    unsigned char htype;
    unsigned char hlen;
    unsigned char hops;
    unsigned int xid;
    unsigned short secs;
    unsigned short flags;
    unsigned int ciaddr;
    unsigned int yiaddr;
    unsigned int siaddr;
    unsigned int giaddr;
    unsigned char chaddr[16];
    unsigned char sname[64];
    unsigned char file[128];
    unsigned char options[312];
};

struct dhcp_lease {
    unsigned int yiaddr;
    unsigned int server;
    unsigned int mask;
    unsigned int router;
    unsigned int dns[3];
    int dns_count;
};

static char *ifname;
static int verbose;

static void
usage()
{
    fprintf(stderr, "usage: dhclient [-v] interface\n");
    exit(1);
}

static void
put32(cp, value)
    unsigned char **cp;
    unsigned int value;
{
    value = ntohl(value);
    *(*cp)++ = (value >> 24) & 0xff;
    *(*cp)++ = (value >> 16) & 0xff;
    *(*cp)++ = (value >> 8) & 0xff;
    *(*cp)++ = value & 0xff;
}

static unsigned int
classful_mask(addr)
    unsigned int addr;
{
    unsigned int h;

    h = ntohl(addr);
    if ((h & 0x80000000L) == 0)
        return htonl(0xff000000L);
    if ((h & 0xc0000000L) == 0x80000000L)
        return htonl(0xffff0000L);
    return htonl(0xffffff00L);
}

static char *
iptoa(addr)
    unsigned int addr;
{
    static char bufs[4][16];
    static int slot;
    unsigned int h;
    char *buf;

    slot = (slot + 1) & 3;
    buf = bufs[slot];
    h = ntohl(addr);
    sprintf(buf, "%u.%u.%u.%u",
        (h >> 24) & 0xff, (h >> 16) & 0xff,
        (h >> 8) & 0xff, h & 0xff);
    return buf;
}

static unsigned int
make_broadcast(addr, mask)
    unsigned int addr;
    unsigned int mask;
{
    unsigned int haddr, hmask;

    haddr = ntohl(addr);
    hmask = ntohl(mask);
    return htonl((haddr & hmask) | (~hmask & 0xffffffffL));
}

static unsigned int
getopt_addr(data, len)
    unsigned char *data;
    int len;
{
    unsigned int value;

    if (len < 4)
        return 0;
    value = ((unsigned int)data[0] << 24) |
        ((unsigned int)data[1] << 16) |
        ((unsigned int)data[2] << 8) |
        (unsigned int)data[3];
    return htonl(value);
}

static int
parse_options(pkt, lease)
    struct dhcp_packet *pkt;
    struct dhcp_lease *lease;
{
    unsigned char *cp, *end;
    int code, len, msgtype;

    msgtype = 0;
    cp = pkt->options;
    end = pkt->options + sizeof(pkt->options);

    if (cp[0] != 99 || cp[1] != 130 || cp[2] != 83 || cp[3] != 99)
        return 0;
    cp += 4;

    while (cp < end) {
        code = *cp++;
        if (code == DHCP_OPT_PAD)
            continue;
        if (code == DHCP_OPT_END)
            break;
        if (cp >= end)
            break;
        len = *cp++;
        if (cp + len > end)
            break;

        switch (code) {
        case DHCP_OPT_MESSAGE_TYPE:
            if (len >= 1)
                msgtype = cp[0];
            break;
        case DHCP_OPT_SERVER_ID:
            lease->server = getopt_addr(cp, len);
            break;
        case DHCP_OPT_SUBNET_MASK:
            lease->mask = getopt_addr(cp, len);
            break;
        case DHCP_OPT_ROUTER:
            lease->router = getopt_addr(cp, len);
            break;
        case DHCP_OPT_DNS:
            lease->dns_count = 0;
            while (len >= 4 && lease->dns_count < 3) {
                lease->dns[lease->dns_count++] = getopt_addr(cp, 4);
                cp += 4;
                len -= 4;
            }
            continue;
        }
        cp += len;
    }
    return msgtype;
}

static void
init_packet(pkt, xid, mac)
    struct dhcp_packet *pkt;
    unsigned int xid;
    unsigned char *mac;
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->op = DHCP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHER;
    pkt->hlen = DHCP_HLEN_ETHER;
    pkt->xid = htonl(xid);
    pkt->flags = htons(0x8000);
    memcpy(pkt->chaddr, mac, DHCP_HLEN_ETHER);
}

static int
make_request(pkt, xid, mac, msgtype, requested, server)
    struct dhcp_packet *pkt;
    unsigned int xid;
    unsigned char *mac;
    int msgtype;
    unsigned int requested;
    unsigned int server;
{
    unsigned char *cp;
    static unsigned char params[] = {
        DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS
    };

    init_packet(pkt, xid, mac);
    cp = pkt->options;
    *cp++ = 99;
    *cp++ = 130;
    *cp++ = 83;
    *cp++ = 99;

    *cp++ = DHCP_OPT_MESSAGE_TYPE;
    *cp++ = 1;
    *cp++ = msgtype;

    *cp++ = DHCP_OPT_CLIENT_ID;
    *cp++ = DHCP_HLEN_ETHER + 1;
    *cp++ = DHCP_HTYPE_ETHER;
    memcpy(cp, mac, DHCP_HLEN_ETHER);
    cp += DHCP_HLEN_ETHER;

    if (requested != 0) {
        *cp++ = DHCP_OPT_REQUESTED_IP;
        *cp++ = 4;
        put32(&cp, requested);
    }
    if (server != 0) {
        *cp++ = DHCP_OPT_SERVER_ID;
        *cp++ = 4;
        put32(&cp, server);
    }

    *cp++ = DHCP_OPT_PARAM_REQ;
    *cp++ = sizeof(params);
    memcpy(cp, params, sizeof(params));
    cp += sizeof(params);

    *cp++ = DHCP_OPT_END;
    return cp - (unsigned char *)pkt;
}

static int
send_dhcp(fd, pkt, len)
    int fd;
    struct dhcp_packet *pkt;
    int len;
{
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DHCP_SERVER_PORT);
    sin.sin_addr.s_addr = INADDR_BROADCAST;

    return sendto(fd, (char *)pkt, len, 0, (struct sockaddr *)&sin,
        sizeof(sin));
}

static int
recv_dhcp(fd, xid, want, lease)
    int fd;
    unsigned int xid;
    int want;
    struct dhcp_lease *lease;
{
    struct dhcp_packet pkt;
    struct sockaddr_in from;
    struct timeval tv;
    int fromlen, n, msgtype;
    fd_set rfds;

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 4;
        tv.tv_usec = 0;
        n = select(fd + 1, &rfds, (fd_set *)0, (fd_set *)0, &tv);
        if (n < 0) {
            perror("dhclient: select");
            return 0;
        }
        if (n == 0)
            return 0;

        fromlen = sizeof(from);
        n = recvfrom(fd, (char *)&pkt, sizeof(pkt), 0,
            (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            perror("dhclient: recvfrom");
            return 0;
        }
        if (n < 240 || pkt.op != DHCP_BOOTREPLY ||
            ntohl(pkt.xid) != xid)
            continue;

        memset(lease, 0, sizeof(*lease));
        lease->yiaddr = pkt.yiaddr;
        msgtype = parse_options(&pkt, lease);
        if (msgtype == want)
            return 1;
    }
}

static int
run(cmd)
    char *cmd;
{
    int rc;

    if (verbose)
        printf("%s\n", cmd);
    rc = system(cmd);
    if (rc != 0)
        fprintf(stderr, "dhclient: command failed: %s\n", cmd);
    return rc == 0;
}

static int
get_hwaddr(mac)
    unsigned char *mac;
{
    struct ifreq ifr;
    int fd;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 0;
    if (ioctl(fd, SIOCGIFHWADDR, (char *)&ifr) < 0) {
        close(fd);
        return 0;
    }
    close(fd);
    if (ifr.ifr_addr.sa_family != AF_UNSPEC)
        return 0;
    memcpy(mac, ifr.ifr_addr.sa_data, DHCP_HLEN_ETHER);
    return 1;
}

static int
apply_lease(lease)
    struct dhcp_lease *lease;
{
    FILE *fp;
    char cmd[180];
    unsigned int mask, bcast;
    int i;

    mask = lease->mask ? lease->mask : classful_mask(lease->yiaddr);
    bcast = make_broadcast(lease->yiaddr, mask);

    sprintf(cmd, "/sbin/ifconfig %s inet %s netmask %s broadcast %s up",
        ifname, iptoa(lease->yiaddr), iptoa(mask), iptoa(bcast));
    if (!run(cmd))
        return 0;

    if (lease->router) {
        sprintf(cmd, "/sbin/route delete default %s", iptoa(lease->router));
        system(cmd);
        sprintf(cmd, "/sbin/route add default %s 1", iptoa(lease->router));
        if (!run(cmd))
            return 0;
    }

    fp = fopen("/var/run/resolv.conf", "w");
    if (fp == NULL) {
        perror("dhclient: /var/run/resolv.conf");
        return 0;
    }
    for (i = 0; i < lease->dns_count; i++)
        fprintf(fp, "nameserver %s\n", iptoa(lease->dns[i]));
    fclose(fp);

    fp = fopen("/var/run/dhclient.lease", "w");
    if (fp != NULL) {
        fprintf(fp, "interface %s\n", ifname);
        fprintf(fp, "address %s\n", iptoa(lease->yiaddr));
        fprintf(fp, "netmask %s\n", iptoa(mask));
        if (lease->router)
            fprintf(fp, "router %s\n", iptoa(lease->router));
        for (i = 0; i < lease->dns_count; i++)
            fprintf(fp, "nameserver %s\n", iptoa(lease->dns[i]));
        fclose(fp);
    }

    printf("%s: address %s", ifname, iptoa(lease->yiaddr));
    printf(" netmask %s", iptoa(mask));
    if (lease->router)
        printf(" router %s", iptoa(lease->router));
    if (lease->dns_count > 0)
        printf(" dns %s", iptoa(lease->dns[0]));
    printf("\n");
    return 1;
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct sockaddr_in local;
    struct dhcp_packet pkt;
    struct dhcp_lease offer, ack;
    unsigned char mac[DHCP_HLEN_ETHER];
    unsigned int xid;
    int fd, i, len, on;
    char cmd[80];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0)
            verbose = 1;
        else if (argv[i][0] == '-')
            usage();
        else
            ifname = argv[i];
    }
    if (ifname == NULL)
        usage();

    xid = ((unsigned int)getpid() << 16) ^ (unsigned int)time((time_t *)0);
    if (!get_hwaddr(mac)) {
        mac[0] = 0x02;
        mac[1] = 0x52;
        mac[2] = 0x42;
        mac[3] = 0x44;
        mac[4] = (xid >> 8) & 0xff;
        mac[5] = xid & 0xff;
    }
    if (verbose)
        printf("dhclient: chaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    sprintf(cmd, "/sbin/ifconfig %s inet 0.0.0.0 netmask 0.0.0.0 up",
        ifname);
    run(cmd);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("dhclient: socket");
        return 1;
    }
    on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof(on));

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(DHCP_CLIENT_PORT);
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("dhclient: bind");
        close(fd);
        return 1;
    }

    len = make_request(&pkt, xid, mac, DHCP_DISCOVER, 0, 0);
    if (verbose)
        printf("dhclient: discover xid=%x\n", xid);
    if (send_dhcp(fd, &pkt, len) != len) {
        perror("dhclient: send discover");
        close(fd);
        return 1;
    }
    if (!recv_dhcp(fd, xid, DHCP_OFFER, &offer)) {
        fprintf(stderr, "dhclient: no offer\n");
        close(fd);
        return 1;
    }
    if (verbose)
        printf("dhclient: offer %s server %s\n", iptoa(offer.yiaddr),
            iptoa(offer.server));

    len = make_request(&pkt, xid, mac, DHCP_REQUEST, offer.yiaddr,
        offer.server);
    if (send_dhcp(fd, &pkt, len) != len) {
        perror("dhclient: send request");
        close(fd);
        return 1;
    }
    if (!recv_dhcp(fd, xid, DHCP_ACK, &ack)) {
        fprintf(stderr, "dhclient: no ack\n");
        close(fd);
        return 1;
    }
    close(fd);

    if (ack.yiaddr == 0)
        ack.yiaddr = offer.yiaddr;
    if (ack.server == 0)
        ack.server = offer.server;
    return apply_lease(&ack) ? 0 : 1;
}
