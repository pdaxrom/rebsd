#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY 2
#define DHCP_HTYPE_ETHER 1
#define DHCP_HLEN_ETHER 6
#define DHCP_OPT_MESSAGE_TYPE 53
#define DHCP_OPT_CLIENT_ID 61
#define DHCP_OPT_PARAM_REQ 55
#define DHCP_OPT_END 255
#define DHCP_DISCOVER 1
#define IFNAMSIZ 16

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

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        short ifru_flags;
        int ifru_metric;
        char *ifru_data;
    } ifr_ifru;
};
#define ifr_addr ifr_ifru.ifru_addr

static void
put32(cpp, value)
    unsigned char **cpp;
    unsigned int value;
{
    unsigned char *cp;

    cp = *cpp;
    *cp++ = (value >> 24) & 0xff;
    *cp++ = (value >> 16) & 0xff;
    *cp++ = (value >> 8) & 0xff;
    *cp++ = value & 0xff;
    *cpp = cp;
}

static int
get_hwaddr(ifname, mac)
    char *ifname;
    unsigned char *mac;
{
    struct ifreq ifr;
    int fd;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket hwaddr");
        return 0;
    }
    if (ioctl(fd, SIOCGIFHWADDR, (char *)&ifr) < 0) {
        perror("ioctl hwaddr");
        close(fd);
        return 0;
    }
    close(fd);
    memcpy(mac, ifr.ifr_addr.sa_data, DHCP_HLEN_ETHER);
    return 1;
}

static int
make_discover(pkt, xid, mac)
    struct dhcp_packet *pkt;
    unsigned int xid;
    unsigned char *mac;
{
    unsigned char *cp;
    static unsigned char params[] = { 1, 3, 6 };

    memset(pkt, 0, sizeof(*pkt));
    pkt->op = DHCP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHER;
    pkt->hlen = DHCP_HLEN_ETHER;
    pkt->xid = htonl(xid);
    memcpy(pkt->chaddr, mac, DHCP_HLEN_ETHER);

    cp = pkt->options;
    *cp++ = 99;
    *cp++ = 130;
    *cp++ = 83;
    *cp++ = 99;
    *cp++ = DHCP_OPT_MESSAGE_TYPE;
    *cp++ = 1;
    *cp++ = DHCP_DISCOVER;
    *cp++ = DHCP_OPT_CLIENT_ID;
    *cp++ = DHCP_HLEN_ETHER + 1;
    *cp++ = DHCP_HTYPE_ETHER;
    memcpy(cp, mac, DHCP_HLEN_ETHER);
    cp += DHCP_HLEN_ETHER;
    *cp++ = DHCP_OPT_PARAM_REQ;
    *cp++ = sizeof(params);
    memcpy(cp, params, sizeof(params));
    cp += sizeof(params);
    *cp++ = DHCP_OPT_END;
    return cp - (unsigned char *)pkt;
}

static int
send_discover(fd, xid, mac)
    int fd;
    unsigned int xid;
    unsigned char *mac;
{
    struct dhcp_packet pkt;
    struct sockaddr_in sin;
    int len, n;

    len = make_discover(&pkt, xid, mac);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DHCP_SERVER_PORT);
    sin.sin_addr.s_addr = INADDR_BROADCAST;
    n = sendto(fd, (char *)&pkt, len, 0, (struct sockaddr *)&sin,
        sizeof(sin));
    printf("sendto len=%d rc=%d errno=%d\n", len, n, errno);
    return n == len;
}

static void
print_fionread(fd, tag)
    int fd;
    char *tag;
{
    long nread;

    nread = -1;
    errno = 0;
    if (ioctl(fd, FIONREAD, (char *)&nread) < 0)
        printf("%s fionread rc=-1 errno=%d\n", tag, errno);
    else
        printf("%s fionread=%ld\n", tag, nread);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct sockaddr_in local, from;
    struct dhcp_packet pkt;
    struct timeval tv;
    fd_set rfds;
    unsigned char mac[DHCP_HLEN_ETHER];
    unsigned int xid;
    char *ifname;
    int fd, fromlen, len, n, on;

    ifname = argc > 1 ? argv[1] : "ne0";
    if (!get_hwaddr(ifname, mac))
        return 1;
    printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket udp");
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
        perror("bind udp/68");
        close(fd);
        return 1;
    }

    xid = ((unsigned int)getpid() << 16) ^ (unsigned int)time((time_t *)0);
    printf("xid=%x\n", xid);
    if (!send_discover(fd, xid, mac)) {
        close(fd);
        return 1;
    }

    print_fionread(fd, "before-select");
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 4;
    tv.tv_usec = 0;
    errno = 0;
    n = select(fd + 1, &rfds, (fd_set *)0, (fd_set *)0, &tv);
    printf("select rc=%d errno=%d isset=%d\n", n, errno,
        FD_ISSET(fd, &rfds));
    print_fionread(fd, "after-select");

    on = 1;
    if (ioctl(fd, FIONBIO, (char *)&on) < 0)
        perror("fionbio on");
    fromlen = sizeof(from);
    errno = 0;
    len = recvfrom(fd, (char *)&pkt, sizeof(pkt), 0,
        (struct sockaddr *)&from, &fromlen);
    printf("recvfrom rc=%d errno=%d fromlen=%d op=%u xid=%x yiaddr=%lx\n",
        len, errno, fromlen, pkt.op, ntohl(pkt.xid), pkt.yiaddr);
    close(fd);

    if (len < 0)
        return 1;
    if (pkt.op != DHCP_BOOTREPLY || ntohl(pkt.xid) != xid)
        return 1;
    return 0;
}
