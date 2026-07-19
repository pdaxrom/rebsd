/*
 * netstat using the bounded KERN_NETINFO sysctl snapshot.
 *
 * This replaces the historical /dev/kmem graph walk.  The kernel validates
 * and flattens its PCB, interface and route lists before copying them out.
 */
#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int Aflag, aflag, iflag, mflag, rflag, sflag, tflag;
static int family = AF_UNSPEC;
static const char *interface;
static int unit = -1;

static void usage(const char *);
static int fetch(struct kinfo_netinfo *);
static void connections(struct kinfo_netinfo *);
static void interfaces(struct kinfo_netinfo *);
static void routes(struct kinfo_netinfo *);
static void stats(struct kinfo_netinfo *, const char *);
static void mbufs(struct kinfo_netinfo *);

int
main(int argc, char **argv)
{
    struct kinfo_netinfo info;
    const char *prog, *proto;
    char *cp;
    int ch, interval;

    prog = argv[0];
    proto = NULL;
    interval = 0;
    while ((ch = getopt(argc, argv, "Aaimnrstup:f:I:")) != -1) {
        switch (ch) {
        case 'A': Aflag = 1; break;
        case 'a': aflag = 1; break;
        case 'i': iflag = 1; break;
        case 'm': mflag = 1; break;
        case 'n': break;
        case 'r': rflag = 1; break;
        case 's': sflag = 1; break;
        case 't': tflag = 1; break;
        case 'u': family = AF_UNIX; break;
        case 'p': proto = optarg; break;
        case 'f':
            if (strcmp(optarg, "inet") == 0) family = AF_INET;
            else if (strcmp(optarg, "unix") == 0) family = AF_UNIX;
            else usage(prog);
            break;
        case 'I':
            iflag = 1;
            interface = optarg;
            cp = (char *)interface;
            while (*cp != '\0' && (*cp < '0' || *cp > '9')) cp++;
            if (*cp != '\0') {
                unit = atoi(cp);
                *cp = '\0';
            }
            break;
        default: usage(prog);
        }
    }
    if (optind < argc) {
        interval = atoi(argv[optind++]);
        if (interval <= 0 || optind != argc)
            usage(prog);
    }
    if (fetch(&info) < 0)
        goto bad;
    if (mflag) {
        mbufs(&info);
        return 0;
    }
    if (rflag) {
        if (sflag) stats(&info, "route"); else routes(&info);
        return 0;
    }
    if (iflag || interval != 0) {
        do {
            interfaces(&info);
            if (interval == 0) break;
            sleep(interval);
            if (fetch(&info) < 0) goto bad;
        } while (1);
        return 0;
    }
    if (sflag || proto != NULL) {
        stats(&info, proto);
        return 0;
    }
    if (family == AF_UNIX) {
        printf("Active UNIX domain sockets are not exported by KERN_NETINFO\n");
        return 0;
    }
    connections(&info);
    return 0;
bad:
    fprintf(stderr, "netstat: KERN_NETINFO: %s\n", strerror(errno));
    return 1;
}

static void
usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-Aaimnrstu] [-f inet|unix] "
        "[-p proto] [-I interface] [interval]\n", prog);
    exit(1);
}

static int
fetch(struct kinfo_netinfo *info)
{
    int mib[2] = { CTL_KERN, KERN_NETINFO };
    size_t size;

    size = sizeof(*info);
    if (sysctl(mib, 2, info, &size, NULL, 0) < 0)
        return -1;
    if (size != sizeof(*info)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void
endpoint(char *buf, size_t len, u_long address, u_short port)
{
    struct in_addr in;
    char host[20];

    if (address == INADDR_ANY)
        strcpy(host, "*");
    else {
        in.s_addr = address;
        strncpy(host, inet_ntoa(in), sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }
    if (port == 0)
        snprintf(buf, len, "%s.*", host);
    else
        snprintf(buf, len, "%s.%u", host, ntohs(port));
}

static void
connections(struct kinfo_netinfo *info)
{
    struct kinfo_netconn *conn;
    char local[32], foreign[32];
    const char *proto, *state;
    int i, shown;

    shown = 0;
    for (i = 0; i < info->kni_nconn; i++) {
        conn = &info->kni_conn[i];
        if (conn->knc_family != AF_INET)
            continue;
        if (!aflag && conn->knc_laddr == INADDR_ANY)
            continue;
        if (!shown++) {
            printf("Active Internet connections%s\n",
                aflag ? " (including servers)" : "");
            if (Aflag) printf("%-8s ", "PCB");
            printf("%-5s %-6s %-6s %-22s %-22s %s\n", "Proto",
                "Recv-Q", "Send-Q", "Local Address", "Foreign Address",
                "(state)");
        }
        proto = conn->knc_protocol == IPPROTO_TCP ? "tcp" :
            conn->knc_protocol == IPPROTO_UDP ? "udp" : "raw";
        endpoint(local, sizeof(local), conn->knc_laddr, conn->knc_lport);
        endpoint(foreign, sizeof(foreign), conn->knc_faddr, conn->knc_fport);
        if (Aflag) printf("%08lx ", conn->knc_pcb);
        printf("%-5s %6lu %6lu %-22.22s %-22.22s", proto,
            conn->knc_recvq, conn->knc_sendq, local, foreign);
        if (conn->knc_protocol == IPPROTO_TCP) {
            state = conn->knc_state >= 0 && conn->knc_state < TCP_NSTATES ?
                tcpstates[conn->knc_state] : "UNKNOWN";
            printf(" %s", state);
        }
        putchar('\n');
    }
    if (!shown)
        printf("No active Internet connections\n");
    if (info->kni_conn_truncated)
        printf("netstat: connection snapshot truncated\n");
}

static void
interfaces(struct kinfo_netinfo *info)
{
    struct kinfo_ifstats *ifp;
    struct in_addr addr;
    char name[12], address[20];
    int i;

    printf("%-6s %-5s %-15s %8s %5s %8s %5s %5s",
        "Name", "Mtu", "Address", "Ipkts", "Ierrs", "Opkts", "Oerrs",
        "Coll");
    if (tflag) printf(" %s", "Time");
    putchar('\n');
    for (i = 0; i < info->kni_nif; i++) {
        ifp = &info->kni_if[i];
        if (interface != NULL && (strcmp(interface, ifp->kif_name) != 0 ||
            (unit >= 0 && unit != ifp->kif_unit)))
            continue;
        snprintf(name, sizeof(name), "%s%d%s", ifp->kif_name,
            ifp->kif_unit, ifp->kif_flags & IFF_UP ? "" : "*");
        if (ifp->kif_addr == 0)
            strcpy(address, "none");
        else {
            addr.s_addr = ifp->kif_addr;
            strncpy(address, inet_ntoa(addr), sizeof(address) - 1);
            address[sizeof(address) - 1] = '\0';
        }
        printf("%-6.6s %-5d %-15.15s %8lu %5lu %8lu %5lu %5lu", name,
            ifp->kif_mtu, address, ifp->kif_ipackets, ifp->kif_ierrors,
            ifp->kif_opackets, ifp->kif_oerrors, ifp->kif_collisions);
        if (tflag) printf(" %d", ifp->kif_timer);
        putchar('\n');
    }
    if (info->kni_if_truncated)
        printf("netstat: interface snapshot truncated\n");
}

static void
routes(struct kinfo_netinfo *info)
{
    struct kinfo_route *rt;
    struct in_addr addr;
    char destination[20], gateway[20], flags[8], *fp;
    int i;

    printf("Routing tables\n");
    printf("%-16s %-16s %-6s %6s %8s %s\n", "Destination", "Gateway",
        "Flags", "Refs", "Use", "Interface");
    for (i = 0; i < info->kni_nroute; i++) {
        rt = &info->kni_route[i];
        if (rt->knr_family != AF_INET)
            continue;
        if (rt->knr_destination == 0)
            strcpy(destination, "default");
        else {
            addr.s_addr = rt->knr_destination;
            strncpy(destination, inet_ntoa(addr), sizeof(destination) - 1);
            destination[sizeof(destination) - 1] = '\0';
        }
        addr.s_addr = rt->knr_gateway;
        strncpy(gateway, inet_ntoa(addr), sizeof(gateway) - 1);
        gateway[sizeof(gateway) - 1] = '\0';
        fp = flags;
        if (rt->knr_flags & RTF_UP) *fp++ = 'U';
        if (rt->knr_flags & RTF_GATEWAY) *fp++ = 'G';
        if (rt->knr_flags & RTF_HOST) *fp++ = 'H';
        if (rt->knr_flags & RTF_DYNAMIC) *fp++ = 'D';
        if (rt->knr_flags & RTF_MODIFIED) *fp++ = 'M';
        *fp = '\0';
        printf("%-16.16s %-16.16s %-6s %6d %8lu %s%d\n", destination,
            gateway, flags, rt->knr_refcnt, rt->knr_use, rt->knr_ifname,
            rt->knr_ifunit);
    }
    if (info->kni_route_truncated)
        printf("netstat: route snapshot truncated\n");
}

static void
stats(struct kinfo_netinfo *info, const char *only)
{
    struct ipstat ip;
    struct tcpstat tcp;
    struct udpstat udp;
    struct icmpstat icmp;
    short *rt;

    memset(&ip, 0, sizeof(ip)); memset(&tcp, 0, sizeof(tcp));
    memset(&udp, 0, sizeof(udp)); memset(&icmp, 0, sizeof(icmp));
    memcpy(&ip, info->kni_stats.kns_ip, sizeof(ip));
    memcpy(&tcp, info->kni_stats.kns_tcp, sizeof(tcp));
    memcpy(&udp, info->kni_stats.kns_udp, sizeof(udp));
    memcpy(&icmp, info->kni_stats.kns_icmp, sizeof(icmp));
    if (only == NULL || strcmp(only, "tcp") == 0)
        printf("tcp:\n\t%lu packets sent, %lu packets received\n"
            "\t%lu connections established, %lu dropped\n"
            "\t%lu data bytes sent, %lu received\n",
            tcp.tcps_sndtotal, tcp.tcps_rcvtotal, tcp.tcps_connects,
            tcp.tcps_drops, tcp.tcps_sndbyte, tcp.tcps_rcvbyte);
    if (only == NULL || strcmp(only, "udp") == 0)
        printf("udp:\n\t%ld packets received, %ld packets sent\n"
            "\t%ld no-port packets, %ld checksum errors\n",
            udp.udps_ipackets, udp.udps_opackets, udp.udps_noport,
            udp.udps_badsum);
    if (only == NULL || strcmp(only, "ip") == 0)
        printf("ip:\n\t%ld packets received, %ld forwarded\n"
            "\t%ld checksum errors, %ld fragments dropped\n",
            ip.ips_total, ip.ips_forward, ip.ips_badsum,
            ip.ips_fragdropped);
    if (only == NULL || strcmp(only, "icmp") == 0)
        printf("icmp:\n\t%ld errors generated, %ld replies generated\n"
            "\t%ld checksum errors\n", icmp.icps_error,
            icmp.icps_reflect, icmp.icps_checksum);
    if (only != NULL && strcmp(only, "route") == 0) {
        rt = info->kni_stats.kns_route;
        printf("routing:\n\t%d bad redirects, %d dynamic routes\n"
            "\t%d new gateways, %d unreachable lookups, %d wildcards\n",
            rt[0], rt[1], rt[2], rt[3], rt[4]);
    }
}

static void
mbufs(struct kinfo_netinfo *info)
{
    struct kinfo_netstats *st;
    unsigned used;

    st = &info->kni_stats;
    used = st->kns_mbufs - st->kns_mtypes[MT_FREE];
    printf("%u/%u mbufs in use\n", used, st->kns_mbufs);
    printf("%u/%u mapped pages in use\n",
        st->kns_clusters - st->kns_clfree, st->kns_clusters);
    printf("%u interface pages allocated\n", st->kns_space);
    printf("%u requests denied, %u delayed, %u protocol drains\n",
        st->kns_drops, st->kns_wait, st->kns_drain);
}
