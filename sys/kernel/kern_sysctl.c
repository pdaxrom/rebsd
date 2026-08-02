/*
 * sysctl system call.
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/dk.h>
#include <sys/vm.h>
#include <sys/map.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>
#include <sys/hw_inventory_provider.h>
#include <sys/rebsd_version.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_shm.h>
#include <vm/vm_sysv_shm.h>
#include <vm/vmspace.h>
#include <vm/pmap.h>
#include <machine/cpu.h>
#include <sys/conf.h>

extern struct tty cnttys[];

#ifdef INET
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/icmp_var.h>
extern int rthashsize;
#endif

#ifndef HW_MACHINE_NAME
#define HW_MACHINE_NAME "mips"
#endif
#ifndef HW_MODEL_NAME
#define HW_MODEL_NAME "mips"
#endif

extern const char rebsd_compiler[];
extern const char rebsd_builduser[];
extern const char rebsd_buildhost[];
extern const int rebsd_build;
extern const char rebsd_toolchain[];
extern const char rebsd_toolchain_version[];
extern const char rebsd_gitrev[];
extern const char rebsd_branch[];
extern const int rebsd_dirty;
extern const char rebsd_buildinfo[];
extern const char rebsd_cpu[];
extern const char rebsd_fpu[];

sysctlfn kern_sysctl;
sysctlfn hw_sysctl;
#ifdef DEBUG
sysctlfn debug_sysctl;
#endif
sysctlfn vm_sysctl;
sysctlfn fs_sysctl;
#ifdef  INET
sysctlfn net_sysctl;
#endif
sysctlfn cpu_sysctl;

struct sysctl_args {
    int     *name;
    u_int   namelen;
    void    *old;
    size_t  *oldlenp;
    void    *new;
    size_t  newlen;
};

struct sysctl_lock memlock;
long hostid;
char hostname[MAXHOSTNAMELEN];
int hostnamelen;

static hw_usb_inventory_provider_t hw_usb_inventory_provider;
static hw_pci_inventory_provider_t hw_pci_inventory_provider;

static int sysctl_clockrate (char *where, size_t *sizep);
static int sysctl_inode (char *where, size_t *sizep);
static int sysctl_file (char *where, size_t *sizep);
static int sysctl_doproc (int *name, u_int namelen, char *where, size_t *sizep);
static int sysctl_procfiles(char *where, size_t *sizep);
#ifdef INET
static int sysctl_netinfo(void *, size_t *, void *);
#endif

void
hw_inventory_register_usb(hw_usb_inventory_provider_t provider)
{
    hw_usb_inventory_provider = provider;
}

void
hw_inventory_register_pci(hw_pci_inventory_provider_t provider)
{
    hw_pci_inventory_provider = provider;
}

static void
sysctl_diskname(char *dst, const char *name, int unit)
{
    char digits[10];
    int pos, n;

    pos = 0;
    while (*name != '\0' && pos < KINFO_DISKNAMELEN - 1)
        dst[pos++] = *name++;
    n = 0;
    do {
        digits[n++] = '0' + unit % 10;
        unit /= 10;
    } while (unit != 0 && n < (int)sizeof(digits));
    while (n != 0 && pos < KINFO_DISKNAMELEN - 1)
        dst[pos++] = digits[--n];
    dst[pos] = '\0';
}

void
__sysctl()
{
    register struct sysctl_args *uap = (struct sysctl_args*) u.u_arg;
    int error;
    size_t oldlen = 0;
    sysctlfn *fn;
    int name [CTL_MAXNAME];

    if (uap->new != NULL && ! suser())
        return;
    /*
     * all top-level sysctl names are non-terminal
     */
    if (uap->namelen > CTL_MAXNAME || uap->namelen < 2) {
        u.u_error = EINVAL;
        return;
    }
    error = copyin ((caddr_t) uap->name, (caddr_t) &name, uap->namelen * sizeof(int));
    if (error) {
        u.u_error = error;
        return;
    }

    switch (name[0]) {
    case CTL_KERN:
        fn = kern_sysctl;
        break;
    case CTL_HW:
        fn = hw_sysctl;
        break;
    case CTL_VM:
        fn = vm_sysctl;
        break;
#ifdef  INET
    case CTL_NET:
        fn = net_sysctl;
        break;
#endif
#ifdef notyet
    case CTL_FS:
        fn = fs_sysctl;
        break;
#endif
    case CTL_MACHDEP:
        fn = cpu_sysctl;
        break;
#ifdef DEBUG
    case CTL_DEBUG:
        fn = debug_sysctl;
        break;
#endif
    default:
        u.u_error = EOPNOTSUPP;
        return;
    }

    if (uap->oldlenp && (error = copyin ((caddr_t) uap->oldlenp,
        (caddr_t) &oldlen, sizeof(oldlen)))) {
        u.u_error = error;
        return;
    }
    if (uap->old != NULL) {
        while (memlock.sl_lock) {
            memlock.sl_want = 1;
            sleep((caddr_t)&memlock, PRIBIO+1);
            memlock.sl_locked++;
        }
        memlock.sl_lock = 1;
    }
    error = (*fn) (name + 1, uap->namelen - 1, uap->old, &oldlen,
        uap->new, uap->newlen);
    if (uap->old != NULL) {
        memlock.sl_lock = 0;
        if (memlock.sl_want) {
            memlock.sl_want = 0;
            wakeup((caddr_t)&memlock);
        }
    }
    if (error) {
        u.u_error = error;
        return;
    }
    if (uap->oldlenp) {
        error = copyout ((caddr_t) &oldlen, (caddr_t) uap->oldlenp, sizeof(oldlen));
        if (error) {
            u.u_error = error;
            return;
        }
    }
    u.u_rval = oldlen;
}

/*
 * kernel related system variables.
 */
int
kern_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    int error, level;
    u_long longhostid;

    /* KERN_TOOLCHAIN is both a readable node and a parent for version. */
    if (namelen != 1 && !(name[0] == KERN_PROC ||
        name[0] == KERN_PROF || name[0] == KERN_TOOLCHAIN))
        return (ENOTDIR);       /* overloaded */

    switch (name[0]) {
    case KERN_OSTYPE:
        return (sysctl_rdstring(oldp, oldlenp, newp, REBSD_OSTYPE));
    case KERN_OSRELEASE:
        return (sysctl_rdstring(oldp, oldlenp, newp, REBSD_OSRELEASE));
    case KERN_OSREV:
        return (sysctl_rdlong(oldp, oldlenp, newp, (long)BSD));
    case KERN_VERSION:
        return (sysctl_rdstring(oldp, oldlenp, newp, version));
    case KERN_MAXINODES:
        return(sysctl_rdint(oldp, oldlenp, newp, NINODE));
    case KERN_MAXPROC:
        return (sysctl_rdint(oldp, oldlenp, newp, NPROC));
    case KERN_MAXFILES:
        return (sysctl_rdint(oldp, oldlenp, newp, NFILE));
    case KERN_ARGMAX:
        return (sysctl_rdint(oldp, oldlenp, newp, NCARGS));
    case KERN_SECURELVL:
        level = securelevel;
        if ((error = sysctl_int(oldp, oldlenp, newp, newlen, &level)) ||
            newp == NULL)
            return (error);
        if (level < securelevel && u.u_procp->p_pid != 1)
            return (EPERM);
        securelevel = level;
        return (0);
    case KERN_HOSTNAME:
        error = sysctl_string(oldp, oldlenp, newp, newlen,
            hostname, sizeof(hostname));
        if (newp && !error)
            hostnamelen = newlen;
        return (error);
    case KERN_HOSTID:
        longhostid = hostid;
        error =  sysctl_long(oldp, oldlenp, newp, newlen, (long*) &longhostid);
        hostid = longhostid;
        return (error);
    case KERN_CLOCKRATE:
        return (sysctl_clockrate(oldp, oldlenp));
    case KERN_BOOTTIME:
        return (sysctl_rdstruct(oldp, oldlenp, newp, &boottime,
            sizeof(struct timeval)));
    case KERN_INODE:
        return (sysctl_inode(oldp, oldlenp));
    case KERN_PROC:
        return (sysctl_doproc(name + 1, namelen - 1, oldp, oldlenp));
    case KERN_FILE:
        return (sysctl_file(oldp, oldlenp));
#ifdef GPROF
    case KERN_PROF:
        return (sysctl_doprof(name + 1, namelen - 1, oldp, oldlenp,
            newp, newlen));
#endif
    case KERN_NGROUPS:
        return (sysctl_rdint(oldp, oldlenp, newp, NGROUPS));
    case KERN_JOB_CONTROL:
        return (sysctl_rdint(oldp, oldlenp, newp, 1));
    case KERN_POSIX1:
    case KERN_SAVED_IDS:
        return (sysctl_rdint(oldp, oldlenp, newp, 0));
    case KERN_CODENAME:
        return (sysctl_rdstring(oldp, oldlenp, newp, REBSD_CODENAME));
    case KERN_COMPILER:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_compiler));
    case KERN_BUILDUSER:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_builduser));
    case KERN_BUILDHOST:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_buildhost));
    case KERN_BUILD:
        return (sysctl_rdint(oldp, oldlenp, newp, rebsd_build));
    case KERN_TOOLCHAIN:
        if (namelen == 1)
            return (sysctl_rdstring(oldp, oldlenp, newp,
                rebsd_toolchain));
        if (namelen == 2 && name[1] == KERN_TOOLCHAIN_VERSION)
            return (sysctl_rdstring(oldp, oldlenp, newp,
                rebsd_toolchain_version));
        return (ENOTDIR);
    case KERN_GITREV:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_gitrev));
    case KERN_BRANCH:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_branch));
    case KERN_DIRTY:
        return (sysctl_rdint(oldp, oldlenp, newp, rebsd_dirty));
    case KERN_BUILDINFO:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_buildinfo));
    case KERN_TTY:
        return (sysctl_rdstruct(oldp, oldlenp, newp, &cnttys[0],
            sizeof(cnttys[0])));
    case KERN_NETINFO:
#ifdef INET
        return (sysctl_netinfo(oldp, oldlenp, newp));
#else
        return (EOPNOTSUPP);
#endif
    case KERN_PROCFILES:
        return (sysctl_procfiles(oldp, oldlenp));
    case KERN_MSGBUF:
        return (msgbuf_sysctl(oldp, oldlenp, newp));
    default:
        return (EOPNOTSUPP);
    }
    /* NOTREACHED */
}

/*
 * hardware related system variables.
 */
int
hw_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    struct kinfo_usb_inventory *usb_inventory;
    struct kinfo_pci_inventory *pci_inventory;

    (void)newlen;
    /* all sysctl names at this level are terminal */
    if (namelen != 1)
        return (ENOTDIR);       /* overloaded */

    switch (name[0]) {
    case HW_MACHINE:
        return (sysctl_rdstring(oldp, oldlenp, newp, HW_MACHINE_NAME));
    case HW_MODEL:
        return (sysctl_rdstring(oldp, oldlenp, newp, HW_MODEL_NAME));
    case HW_NCPU:
        return (sysctl_rdint(oldp, oldlenp, newp, 1));  /* XXX */
    case HW_BYTEORDER:
        return (sysctl_rdint(oldp, oldlenp, newp, ENDIAN));
    case HW_PHYSMEM:
        return (sysctl_rdlong(oldp, oldlenp, newp, physmem));
    case HW_USERMEM:
        return (sysctl_rdlong(oldp, oldlenp, newp, MAXMEM));
    case HW_PAGESIZE:
        return (sysctl_rdint(oldp, oldlenp, newp, VM_PAGE_SIZE));
    case HW_CPU:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_cpu));
    case HW_FPU:
        return (sysctl_rdstring(oldp, oldlenp, newp, rebsd_fpu));
    case HW_USBDEVICES:
        if (hw_usb_inventory_provider == NULL)
            return (EOPNOTSUPP);
        usb_inventory = hw_usb_inventory_provider();
        if (usb_inventory == NULL)
            return (ENXIO);
        return (sysctl_rdstruct(oldp, oldlenp, newp,
            usb_inventory, sizeof(*usb_inventory)));
    case HW_PCIDEVICES:
        if (hw_pci_inventory_provider == NULL)
            return (EOPNOTSUPP);
        pci_inventory = hw_pci_inventory_provider();
        if (pci_inventory == NULL)
            return (ENXIO);
        return (sysctl_rdstruct(oldp, oldlenp, newp,
            pci_inventory, sizeof(*pci_inventory)));
    default:
        return (EOPNOTSUPP);
    }
    /* NOTREACHED */
}

#ifdef INET
static struct kinfo_netinfo netinfo_snapshot;

static void
netinfo_name(char *dst, const char *src)
{
    int i;

    for (i = 0; i < 7 && src != NULL && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void
netinfo_pcbs(struct kinfo_netinfo *ni, struct inpcb *head, int protocol)
{
    struct kinfo_netconn *conn;
    struct inpcb *inp;
    struct socket *so;
    struct tcpcb *tp;
    int remaining;

    remaining = NFILE + 1;
    for (inp = head->inp_next; inp != head && inp != NULL;
        inp = inp->inp_next) {
        if (--remaining == 0 || ni->kni_nconn >= KINFO_NET_MAXCONN) {
            ni->kni_conn_truncated = 1;
            break;
        }
        so = inp->inp_socket;
        if (so == NULL)
            continue;
        conn = &ni->kni_conn[ni->kni_nconn++];
        conn->knc_pcb = protocol == IPPROTO_TCP ?
            (u_long)inp->inp_ppcb : (u_long)inp;
        conn->knc_family = AF_INET;
        conn->knc_protocol = protocol;
        conn->knc_type = so->so_type;
        conn->knc_laddr = inp->inp_laddr.s_addr;
        conn->knc_faddr = inp->inp_faddr.s_addr;
        conn->knc_lport = inp->inp_lport;
        conn->knc_fport = inp->inp_fport;
        conn->knc_recvq = so->so_rcv.sb_cc;
        conn->knc_sendq = so->so_snd.sb_cc;
        if (protocol == IPPROTO_TCP) {
            tp = (struct tcpcb *)inp->inp_ppcb;
            conn->knc_state = tp != NULL ? tp->t_state : -1;
        }
    }
}

static void
netinfo_interfaces(struct kinfo_netinfo *ni)
{
    struct kinfo_ifstats *dst;
    struct ifaddr *ifa;
    struct ifnet *ifp;
    struct in_ifaddr *ia;
    struct sockaddr_in *sin;

    for (ifp = ifnet; ifp != NULL; ifp = ifp->if_next) {
        if (ni->kni_nif >= KINFO_NET_MAXIF) {
            ni->kni_if_truncated = 1;
            break;
        }
        dst = &ni->kni_if[ni->kni_nif++];
        netinfo_name(dst->kif_name, ifp->if_name);
        dst->kif_unit = ifp->if_unit;
        dst->kif_mtu = ifp->if_mtu;
        dst->kif_flags = ifp->if_flags;
        dst->kif_timer = ifp->if_timer;
        dst->kif_metric = ifp->if_metric;
        dst->kif_snd_len = ifp->if_snd.ifq_len;
        dst->kif_snd_drops = ifp->if_snd.ifq_drops;
        dst->kif_ipackets = ifp->if_ipackets;
        dst->kif_ierrors = ifp->if_ierrors;
        dst->kif_opackets = ifp->if_opackets;
        dst->kif_oerrors = ifp->if_oerrors;
        dst->kif_collisions = ifp->if_collisions;
        for (ifa = ifp->if_addrlist; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr.sa_family != AF_INET)
                continue;
            ia = (struct in_ifaddr *)ifa;
            sin = (struct sockaddr_in *)&ia->ia_addr;
            dst->kif_addr = sin->sin_addr.s_addr;
            dst->kif_subnet = ia->ia_subnet;
            dst->kif_subnetmask = ia->ia_subnetmask;
            break;
        }
    }
}

static void
netinfo_routes(struct kinfo_netinfo *ni, struct mbuf **table)
{
    struct kinfo_route *dst;
    struct sockaddr_in *sin;
    struct rtentry *rt;
    struct mbuf *m;
    int bucket, remaining;

    if (rthashsize <= 0 || rthashsize > 4096)
        return;
    for (bucket = 0; bucket < rthashsize; bucket++) {
        remaining = NFILE + 1;
        for (m = table[bucket]; m != NULL; m = m->m_next) {
            if (--remaining == 0 ||
                ni->kni_nroute >= KINFO_NET_MAXROUTE) {
                ni->kni_route_truncated = 1;
                return;
            }
            rt = mtod(m, struct rtentry *);
            dst = &ni->kni_route[ni->kni_nroute++];
            dst->knr_family = rt->rt_dst.sa_family;
            if (dst->knr_family == AF_INET) {
                sin = (struct sockaddr_in *)&rt->rt_dst;
                dst->knr_destination = sin->sin_addr.s_addr;
                sin = (struct sockaddr_in *)&rt->rt_gateway;
                dst->knr_gateway = sin->sin_addr.s_addr;
            }
            dst->knr_flags = rt->rt_flags;
            dst->knr_refcnt = rt->rt_refcnt;
            dst->knr_use = rt->rt_use;
            if (rt->rt_ifp != NULL) {
                netinfo_name(dst->knr_ifname, rt->rt_ifp->if_name);
                dst->knr_ifunit = rt->rt_ifp->if_unit;
            }
        }
    }
}

static int
sysctl_netinfo(void *oldp, size_t *oldlenp, void *newp)
{
    struct kinfo_netinfo *ni;
    struct kinfo_netstats *stats;

    ni = &netinfo_snapshot;
    bzero(ni, sizeof(*ni));
    netinfo_pcbs(ni, &tcb, IPPROTO_TCP);
    netinfo_pcbs(ni, &udb, IPPROTO_UDP);
    netinfo_interfaces(ni);
    netinfo_routes(ni, rthost);
    netinfo_routes(ni, rtnet);

    stats = &ni->kni_stats;
    stats->kns_mbufs = mbstat.m_mbufs;
    stats->kns_clusters = mbstat.m_clusters;
    stats->kns_space = mbstat.m_space;
    stats->kns_clfree = mbstat.m_clfree;
    stats->kns_drops = mbstat.m_drops;
    stats->kns_wait = mbstat.m_wait;
    stats->kns_drain = mbstat.m_drain;
    bcopy(mbstat.m_mtypes, stats->kns_mtypes,
        sizeof(stats->kns_mtypes));
    bcopy(&ipstat, stats->kns_ip, sizeof(ipstat));
    bcopy(&tcpstat, stats->kns_tcp, sizeof(tcpstat));
    bcopy(&udpstat, stats->kns_udp, sizeof(udpstat));
    bcopy(&icmpstat, stats->kns_icmp, sizeof(icmpstat));
    bcopy(&rtstat, stats->kns_route, sizeof(rtstat));
    return (sysctl_rdstruct(oldp, oldlenp, newp, ni, sizeof(*ni)));
}
#endif /* INET */

#ifdef DEBUG
/*
 * Debugging related system variables.
 */
struct ctldebug debug0, debug1, debug2, debug3, debug4;
struct ctldebug debug5, debug6, debug7, debug8, debug9;
struct ctldebug debug10, debug11, debug12, debug13, debug14;
struct ctldebug debug15, debug16, debug17, debug18, debug19;
static struct ctldebug *debugvars[CTL_DEBUG_MAXID] = {
    &debug0, &debug1, &debug2, &debug3, &debug4,
    &debug5, &debug6, &debug7, &debug8, &debug9,
    &debug10, &debug11, &debug12, &debug13, &debug14,
    &debug15, &debug16, &debug17, &debug18, &debug19,
};

int
debug_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    struct ctldebug *cdp;

    /* all sysctl names at this level are name and field */
    if (namelen != 2)
        return (ENOTDIR);       /* overloaded */
    cdp = debugvars[name[0]];
    if (cdp->debugname == 0)
        return (EOPNOTSUPP);
    switch (name[1]) {
    case CTL_DEBUG_NAME:
        return (sysctl_rdstring(oldp, oldlenp, newp, cdp->debugname));
    case CTL_DEBUG_VALUE:
        return (sysctl_int(oldp, oldlenp, newp, newlen, cdp->debugvar));
    default:
        return (EOPNOTSUPP);
    }
    /* NOTREACHED */
}
#endif /* DEBUG */

/*
 * Bit of a hack.  2.11 currently uses 'short avenrun[3]' and a fixed scale
 * of 256.  In order not to break all the applications which nlist() for
 * 'avenrun' we build a local 'averunnable' structure here to return to the
 * user.  Eventually (after all applications which look up the load average
 * the old way) have been converted we can change things.
 *
 * VM_METER refreshes vmtotal() before copying the cached totals so short
 * lived commands see current memory counters.
 *
 * The swapmap case is 2.11BSD extension.
 */
int
vm_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
    struct  loadavg averunnable;    /* loadavg in resource.h */
    struct kinfo_ucb_stats ucb;
    struct vm_page_stats page_stats;
    struct pmap_stats pmap_stats;
    struct vm_object_stats object_stats;
    struct vm_shm_stats shm_stats;
    struct vm_sysv_shm_stats sysv_shm_stats;
    long page_value;
    int error;
    int reset;
    int i;

    /* all sysctl names at this level are terminal */
    if (namelen != 1)
        return (ENOTDIR);       /* overloaded */

    switch (name[0]) {
    case VM_LOADAVG:
        averunnable.fscale = 256;
        averunnable.ldavg[0] = avenrun[0];
        averunnable.ldavg[1] = avenrun[1];
        averunnable.ldavg[2] = avenrun[2];
        return (sysctl_rdstruct(oldp, oldlenp, newp, &averunnable,
            sizeof(averunnable)));
    case VM_METER:
        vmtotal();
        return (sysctl_rdstruct(oldp, oldlenp, newp, &total,
            sizeof(total)));
    case VM_SWAPMAP:
        if (oldp == NULL) {
            *oldlenp = (char *)swapmap[0].m_limit -
                    (char *)swapmap[0].m_map;
            return(0);
        }
        return (sysctl_rdstruct(oldp, oldlenp, newp, swapmap[0].m_map,
            (int)swapmap[0].m_limit - (int)swapmap[0].m_map));
    case VM_SWAPTOTAL:
        return (sysctl_rdlong(oldp, oldlenp, newp,
            (long)nswap * DEV_BSIZE));
    case VM_UCBSTATS:
        bzero(&ucb, sizeof(ucb));
#ifdef UCB_METER
        ucb.kus_hz = hz;
        ucb.kus_dk_ndrive = dk_ndrive;
        if (ucb.kus_dk_ndrive > KINFO_MAXDISKS)
            ucb.kus_dk_ndrive = KINFO_MAXDISKS;
        ucb.kus_dk_busy = dk_busy;
        for (i = 0; i < KINFO_CPUSTATES; i++)
            ucb.kus_cp_time[i] = cp_time[i];
        for (i = 0; i < ucb.kus_dk_ndrive; i++) {
            ucb.kus_dk_xfer[i] = dk_xfer[i];
            ucb.kus_dk_bytes[i] = dk_bytes[i];
            if (dk_name[i] != NULL)
                sysctl_diskname(ucb.kus_dk_name[i], dk_name[i],
                    dk_unit[i]);
        }
        ucb.kus_tk_nin = tk_nin;
        ucb.kus_tk_nout = tk_nout;
        ucb.kus_rate = rate;
        ucb.kus_sum = sum;
        ucb.kus_forkstat = forkstat;
        ucb.kus_freemem = freemem;
#endif
        vmtotal();
        ucb.kus_total = total;
        ucb.kus_boottime = boottime.tv_sec;
        return (sysctl_rdstruct(oldp, oldlenp, newp, &ucb,
            sizeof(ucb)));
    case VM_UCBRESET:
        reset = 0;
        error = sysctl_int(oldp, oldlenp, newp, newlen, &reset);
        if (error != 0 || newp == NULL || reset == 0)
            return (error);
#ifdef UCB_METER
        bzero(&sum, sizeof(sum));
#endif
        return (0);
    case VM_PHYSPAGES:
    case VM_FREEPAGES:
    case VM_RESERVEDPAGES:
    case VM_PAGEALLOCS:
    case VM_PAGEFREES:
    case VM_PAGEFAILURES:
    case VM_PAGEPOISONFAILURES:
    case VM_BADPAGES:
        error = vm_page_bootstrap_stats(&page_stats);
        if (error != 0)
            return error;
        switch (name[0]) {
        case VM_PHYSPAGES:
            page_value = page_stats.vps_total;
            break;
        case VM_FREEPAGES:
            page_value = page_stats.vps_free;
            break;
        case VM_RESERVEDPAGES:
            page_value = page_stats.vps_reserved;
            break;
        case VM_PAGEALLOCS:
            page_value = page_stats.vps_allocations;
            break;
        case VM_PAGEFREES:
            page_value = page_stats.vps_frees;
            break;
        case VM_PAGEFAILURES:
            page_value = page_stats.vps_allocation_failures;
            break;
        case VM_BADPAGES:
            page_value = page_stats.vps_bad;
            break;
        default:
            page_value = page_stats.vps_poison_failures;
            break;
        }
        return (sysctl_rdlong(oldp, oldlenp, newp, page_value));
    case VM_PMAPMAPPINGS:
    case VM_PMAPRESIDENT:
    case VM_PMAPREFILLS:
    case VM_PMAPMODIFIED:
    case VM_PMAPFAULTS:
    case VM_PMAPTARGETED:
    case VM_PMAPFLUSHES:
    case VM_PMAPROLLOVERS:
        error = pmap_bootstrap_stats(&pmap_stats);
        if (error != 0)
            return error;
        switch (name[0]) {
        case VM_PMAPMAPPINGS:
            page_value = pmap_stats.pms_mappings;
            break;
        case VM_PMAPRESIDENT:
            page_value = pmap_stats.pms_resident_pages;
            break;
        case VM_PMAPREFILLS:
            page_value = pmap_stats.pms_tlb_refills;
            break;
        case VM_PMAPMODIFIED:
            page_value = pmap_stats.pms_tlb_modified;
            break;
        case VM_PMAPFAULTS:
            page_value = pmap_stats.pms_protection_faults;
            break;
        case VM_PMAPTARGETED:
            page_value = pmap_stats.pms_targeted_invalidations;
            break;
        case VM_PMAPFLUSHES:
            page_value = pmap_stats.pms_full_flushes;
            break;
        default:
            page_value = pmap_stats.pms_asid_rollovers;
            break;
        }
        return (sysctl_rdlong(oldp, oldlenp, newp, page_value));
    case VM_OBJECTS:
    case VM_ANONPAGES:
    case VM_OBJECTRESIDENT:
    case VM_OBJECTSWAPPED:
    case VM_ZEROFAULTS:
    case VM_COWFAULTS:
    case VM_PAGEINS:
    case VM_PAGEOUTS:
    case VM_SWAPFAILURES:
    case VM_OBJECTFAULTS:
    case VM_OBJECTWAITS:
    case VM_FAULTWOULDBLOCK:
    case VM_RECLAIMATTEMPTS:
    case VM_RECLAIMFAILURES:
        error = vm_object_get_stats(&object_stats);
        if (error != 0)
            return error;
        switch (name[0]) {
        case VM_OBJECTS:
            page_value = object_stats.vos_objects;
            break;
        case VM_ANONPAGES:
            page_value = object_stats.vos_anon_pages;
            break;
        case VM_OBJECTRESIDENT:
            page_value = object_stats.vos_resident_pages;
            break;
        case VM_OBJECTSWAPPED:
            page_value = object_stats.vos_swapped_pages;
            break;
        case VM_ZEROFAULTS:
            page_value = object_stats.vos_zero_faults;
            break;
        case VM_COWFAULTS:
            page_value = object_stats.vos_cow_faults;
            break;
        case VM_PAGEINS:
            page_value = object_stats.vos_pageins;
            break;
        case VM_PAGEOUTS:
            page_value = object_stats.vos_pageouts;
            break;
        case VM_SWAPFAILURES:
            page_value = object_stats.vos_swap_failures;
            break;
        case VM_OBJECTFAULTS:
            page_value = object_stats.vos_faults;
            break;
        case VM_OBJECTWAITS:
            page_value = object_stats.vos_busy_waits;
            break;
        case VM_FAULTWOULDBLOCK:
            page_value = object_stats.vos_fault_wouldblocks;
            break;
        case VM_RECLAIMATTEMPTS:
            page_value = object_stats.vos_reclaim_attempts;
            break;
        default:
            page_value = object_stats.vos_reclaim_failures;
            break;
        }
        return (sysctl_rdlong(oldp, oldlenp, newp, page_value));
    case VM_SHMOBJECTS:
    case VM_SHMPAGES:
    case VM_SHMMAPPINGS:
    case VM_SHMMAXOBJECTS:
    case VM_SHMMAXPAGES:
    case VM_SHMMAXMAPPINGS:
    case VM_SYSVSEGMENTS:
    case VM_SYSVATTACHMENTS:
        error = vm_shm_get_stats(&shm_stats);
        if (error == 0)
            error = vm_sysv_shm_get_stats(&sysv_shm_stats);
        if (error != 0)
            return error;
        switch (name[0]) {
        case VM_SHMOBJECTS:
            page_value = shm_stats.vss_objects +
                sysv_shm_stats.vsss_segments;
            break;
        case VM_SHMPAGES:
            page_value = shm_stats.vss_pages +
                sysv_shm_stats.vsss_pages;
            break;
        case VM_SHMMAPPINGS:
            page_value = u.u_procp == 0 ? 0 :
                vmspace_shared_mapping_count(u.u_procp->p_vmspace);
            break;
        case VM_SHMMAXOBJECTS:
            page_value = VM_SHM_MAX_OBJECTS + VM_SYSV_SHM_MAX_SEGMENTS;
            break;
        case VM_SHMMAXPAGES:
            page_value = VM_SHM_MAX_BYTES / VM_PAGE_SIZE;
            break;
        case VM_SHMMAXMAPPINGS:
            page_value = VM_MAP_MAX_ENTRIES;
            break;
        case VM_SYSVSEGMENTS:
            page_value = sysv_shm_stats.vsss_segments;
            break;
        default:
            page_value = sysv_shm_stats.vsss_attachments;
            break;
        }
        return (sysctl_rdlong(oldp, oldlenp, newp, page_value));
    default:
        return (EOPNOTSUPP);
    }
    /* NOTREACHED */
}

/*
 * Validate parameters and get old / set new parameters
 * for an integer-valued sysctl function.
 */
int
sysctl_int(void *oldp, size_t *oldlenp, void *newp, size_t newlen, int *valp)
{
    int error = 0;

    if (oldp && *oldlenp < sizeof(int))
        return (ENOMEM);
    if (newp && newlen != sizeof(int))
        return (EINVAL);
    *oldlenp = sizeof(int);
    if (oldp)
        error = copyout ((caddr_t) valp, (caddr_t) oldp, sizeof(int));
    if (error == 0 && newp)
        error = copyin ((caddr_t) newp, (caddr_t) valp, sizeof(int));
    return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdint(void *oldp, size_t *oldlenp, void *newp, int val)
{
    int error = 0;

    if (oldp && *oldlenp < sizeof(int))
        return (ENOMEM);
    if (newp)
        return (EPERM);
    *oldlenp = sizeof(int);
    if (oldp)
        error = copyout((caddr_t)&val, oldp, sizeof(int));
    return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for an long-valued sysctl function.
 */
int
sysctl_long(void *oldp, size_t *oldlenp, void *newp, size_t newlen, long *valp)
{
    int error = 0;

    if (oldp && *oldlenp < sizeof(long))
        return (ENOMEM);
    if (newp && newlen != sizeof(long))
        return (EINVAL);
    *oldlenp = sizeof(long);
    if (oldp)
        error = copyout ((caddr_t) valp, (caddr_t) oldp, sizeof(long));
    if (error == 0 && newp)
        error = copyin ((caddr_t) newp, (caddr_t) valp, sizeof(long));
    return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdlong(void *oldp, size_t *oldlenp, void *newp, long val)
{
    int error = 0;

    if (oldp && *oldlenp < sizeof(long))
        return (ENOMEM);
    if (newp)
        return (EPERM);
    *oldlenp = sizeof(long);
    if (oldp)
        error = copyout((caddr_t)&val, oldp, sizeof(long));
    return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a string-valued sysctl function.
 */
int
sysctl_string(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    char *str, size_t maxlen)
{
    size_t len;
    int error = 0;

    len = strlen(str) + 1;
    if (oldp && *oldlenp < len)
        return (ENOMEM);
    if (newp && newlen >= maxlen)
        return (EINVAL);
    if (oldp) {
        *oldlenp = len;
        error = copyout (str, oldp, len);
    }
    if (error == 0 && newp) {
        error = copyin (newp, str, newlen);
        str[newlen] = 0;
    }
    return (error);
}

/*
 * As above, but read-only.
 */
int
sysctl_rdstring(void *oldp, size_t *oldlenp, void *newp, const char *str)
{
    size_t len;
    int error = 0;

    len = strlen(str) + 1;
    if (oldp && *oldlenp < len)
        return (ENOMEM);
    if (newp)
        return (EPERM);
    *oldlenp = len;
    if (oldp)
        error = copyout ((caddr_t) str, oldp, len);
    return (error);
}

/*
 * Validate parameters and get old / set new parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_struct(void *oldp, size_t *oldlenp, void *newp, size_t newlen,
    void *sp, size_t len)
{
    int error = 0;

    if (oldp && *oldlenp < len)
        return (ENOMEM);
    if (newp && newlen > len)
        return (EINVAL);
    if (oldp) {
        *oldlenp = len;
        error = copyout(sp, oldp, len);
    }
    if (error == 0 && newp)
        error = copyin(newp, sp, len);
    return (error);
}

/*
 * Validate parameters and get old parameters
 * for a structure oriented sysctl function.
 */
int
sysctl_rdstruct(void *oldp, size_t *oldlenp, void *newp, void *sp,
    size_t len)
{
    int error = 0;

    if (oldp && *oldlenp < len)
        return (ENOMEM);
    if (newp)
        return (EPERM);
    *oldlenp = len;
    if (oldp)
        error = copyout(sp, oldp, len);
    return (error);
}

/*
 * Get file structures.
 */
int
sysctl_file(char *where, size_t *sizep)
{
    size_t buflen;
    int error;
    register struct file *fp;
    struct  file *fpp;
    char *start = where;
    register int i;

    buflen = *sizep;
    if (where == NULL) {
        for (i = 0, fp = file; fp < file+NFILE; fp++)
            if (fp->f_count) i++;

#define FPTRSZ  sizeof (struct file *)
#define FILESZ  sizeof (struct file)
        /*
         * overestimate by 5 files
         */
        *sizep = (i + 5) * (FILESZ + FPTRSZ);
        return (0);
    }

    /*
     * array of extended file structures: first the address then the
     * file structure.
     */
    for (fp = file; fp < file+NFILE; fp++) {
        if (fp->f_count == 0)
            continue;
        if (buflen < (FPTRSZ + FILESZ)) {
            *sizep = where - start;
            return (ENOMEM);
        }
        fpp = fp;
        if ((error = copyout ((caddr_t) &fpp, (caddr_t) where, FPTRSZ)) ||
            (error = copyout ((caddr_t) fp, (caddr_t) (where + FPTRSZ), FILESZ)))
            return (error);
        buflen -= (FPTRSZ + FILESZ);
        where += (FPTRSZ + FILESZ);
    }
    *sizep = where - start;
    return (0);
}

static int
sysctl_procfile_emit(struct proc *p, struct user *up, int fd,
    struct file *fp, struct inode *ip, char *where)
{
    struct kinfo_procfile kpf;

    bzero(&kpf, sizeof(kpf));
    kpf.kpf_pid = p->p_pid;
    kpf.kpf_uid = p->p_uid;
    kpf.kpf_fd = fd;
    strncpy(kpf.kpf_comm, up->u_comm, sizeof(kpf.kpf_comm) - 1);
    if (fp != NULL) {
        kpf.kpf_type = fp->f_type;
        kpf.kpf_flags = fp->f_flag;
        kpf.kpf_filep = (u_long)fp;
        kpf.kpf_datap = (u_long)fp->f_data;
        kpf.kpf_offset = fp->f_offset;
    } else {
        kpf.kpf_type = DTYPE_INODE;
        kpf.kpf_datap = (u_long)ip;
    }
    if (ip != NULL) {
        kpf.kpf_dev = ip->i_dev;
        kpf.kpf_rdev = ip->i_rdev;
        kpf.kpf_inode = ip->i_number;
        kpf.kpf_mode = ip->i_mode;
        kpf.kpf_size = ip->i_size;
    }
    return copyout((caddr_t)&kpf, where, sizeof(kpf));
}

/*
 * Return flattened per-process descriptors.  No u-area, file, inode, socket,
 * or PCB pointer is accepted from userland; every entry is validated while
 * walking kernel-owned tables.
 */
static int
sysctl_procfiles(char *where, size_t *sizep)
{
    struct inode *ip;
    struct file *fp;
    struct user *up;
    struct proc *p;
    char *start;
    size_t buflen, needed;
    int error, fd;

    start = where;
    buflen = where != NULL ? *sizep : 0;
    needed = 0;
    for (p = allproc; p != NULL; p = p->p_nxt) {
        up = p->p_uarea;
        if (up == NULL)
            continue;
        if (up->u_cdir != NULL) {
            needed += sizeof(struct kinfo_procfile);
            if (buflen >= sizeof(struct kinfo_procfile)) {
                error = sysctl_procfile_emit(p, up, KINFO_FD_CWD,
                    NULL, up->u_cdir, where);
                if (error != 0)
                    return error;
                where += sizeof(struct kinfo_procfile);
                buflen -= sizeof(struct kinfo_procfile);
            }
        }
        for (fd = 0; fd <= up->u_lastfile && fd < NOFILE; fd++) {
            fp = up->u_ofile[fd];
            if (fp == NULL)
                continue;
            ip = NULL;
            if (fp->f_type == DTYPE_INODE || fp->f_type == DTYPE_PIPE)
                ip = (struct inode *)fp->f_data;
            needed += sizeof(struct kinfo_procfile);
            if (buflen >= sizeof(struct kinfo_procfile)) {
                error = sysctl_procfile_emit(p, up, fd, fp, ip, where);
                if (error != 0)
                    return error;
                where += sizeof(struct kinfo_procfile);
                buflen -= sizeof(struct kinfo_procfile);
            }
        }
    }
    if (start == NULL) {
        *sizep = needed + 8 * sizeof(struct kinfo_procfile);
        return 0;
    }
    *sizep = where - start;
    return needed > *sizep ? ENOMEM : 0;
}

/*
 * This one is in kern_clock.c in 4.4 but placed here for the reasons
 * given earlier (back around line 367).
 */
int
sysctl_clockrate (char *where, size_t *sizep)
{
    struct  clockinfo clkinfo;

    /*
     * Construct clockinfo structure.
    */
    clkinfo.hz = hz;
    clkinfo.tick = usechz;
    clkinfo.profhz = 0;
    clkinfo.stathz = hz;
    return(sysctl_rdstruct(where, sizep, NULL, &clkinfo, sizeof (clkinfo)));
}

/*
 * Dump inode list (via sysctl).
 * Copyout address of inode followed by inode.
 */
/* ARGSUSED */
int
sysctl_inode (char *where, size_t *sizep)
{
    register struct inode *ip;
    register char *bp = where;
    struct inode *ipp;
    char *ewhere;
    int error, numi;

    for (numi = 0, ip = inode; ip < inode+NINODE; ip++)
        if (ip->i_count) numi++;

#define IPTRSZ  sizeof (struct inode *)
#define INODESZ sizeof (struct inode)
    if (where == NULL) {
        *sizep = (numi + 5) * (IPTRSZ + INODESZ);
        return (0);
    }
    ewhere = where + *sizep;

    for (ip = inode; ip < inode+NINODE; ip++) {
        if (ip->i_count == 0)
            continue;
        if (bp + IPTRSZ + INODESZ > ewhere) {
            *sizep = bp - where;
            return (ENOMEM);
        }
        ipp = ip;
        if ((error = copyout ((caddr_t)&ipp, bp, IPTRSZ)) ||
            (error = copyout ((caddr_t)ip, bp + IPTRSZ, INODESZ)))
            return (error);
        bp += IPTRSZ + INODESZ;
    }

    *sizep = bp - where;
    return (0);
}

/*
 * Three pieces of information we need about a process are not kept in
 * the proc table: real uid, controlling terminal device, and controlling
 * terminal tty struct pointer.  For these we must look in either the u
 * area or the swap area.  If the process is still in memory this is
 * easy but if the process has been swapped out we have to read in the
 * u area.
 *
 * XXX - We rely on the fact that u_ttyp, u_ttyd, and u_ruid are all within
 * XXX - the first 1kb of the u area.  If this ever changes the logic below
 * XXX - will break (and badly).  At the present time (97/9/2) the u area
 * XXX - is 856 bytes long.
 */
void
fill_from_u (struct proc *p, uid_t *rup, struct tty **ttp, dev_t *tdp,
    char *comm, size_t commlen)
{
    dev_t   ttyd;
    uid_t   ruid;
    struct  tty *ttyp;
    struct  user    *up;

    if (comm && commlen)
        comm[0] = '\0';
    if (p->p_stat == SZOMB) {
        ruid = (uid_t)-2;
        ttyp = NULL;
        ttyd = NODEV;
        if (comm && commlen) {
            strncpy(comm, "zombie", commlen - 1);
            comm[commlen - 1] = '\0';
        }
        goto out;
    }
    up = p->p_uarea;
    if (up != NULL) {
        ttyd = up->u_ttyd;
        ttyp = up->u_ttyp;
        ruid = up->u_ruid;
        if (comm && commlen) {
            strncpy(comm, up->u_comm, commlen - 1);
            comm[commlen - 1] = '\0';
        }
    } else {
        ttyd = NODEV;
        ttyp = NULL;
        ruid = (uid_t)-2;
    }
out:
    if (rup)
        *rup = ruid;
    if (ttp)
        *ttp = ttyp;
    if (tdp)
        *tdp = ttyd;
}

/*
 * Fill in an eproc structure for the specified process.  Slightly
 * inefficient because we have to access the u area again for the
 * information not kept in the proc structure itself.  Can't afford
 * to expand the proc struct so we take a slight speed hit here.
 */
static void
fill_eproc(struct proc *p, struct eproc *ep, char *comm, size_t commlen)
{
    struct  tty *ttyp;

    ep->e_paddr = p;
    fill_from_u(p, &ep->e_ruid, &ttyp, &ep->e_tdev,
        comm, commlen);
    if  (ttyp)
        ep->e_tpgid = ttyp->t_pgrp;
    else
        ep->e_tpgid = 0;
}

/*
 * try over estimating by 5 procs
 */
#define KERN_PROCSLOP   (5 * sizeof (struct kinfo_proc))

int
sysctl_doproc(int *name, u_int namelen, char *where, size_t *sizep)
{
    register struct proc *p;
    register struct kinfo_proc *dp = (struct kinfo_proc *)where;
    struct kinfo_proc kproc;
    struct user *up;
    size_t needed = 0;
    size_t buflen = where != NULL ? *sizep : 0;
    int doingzomb;
    int error = 0;
    dev_t ttyd;
    uid_t ruid;
    struct tty *ttyp;

    if (namelen != 2 && !(namelen == 1 && name[0] == KERN_PROC_ALL))
        return (EINVAL);
    p = (struct proc *)allproc;
    doingzomb = 0;
again:
    for (; p != NULL; p = p->p_nxt) {
        /*
         * Skip embryonic processes.
         */
        if (p->p_stat == SIDL)
            continue;
        /*
         * TODO: sysctl_oproc - make more efficient (see notes below).
         * do by session.
         */
        switch (name[0]) {

        case KERN_PROC_PID:
            /* could do this with just a lookup */
            if (p->p_pid != (pid_t)name[1])
                continue;
            break;

        case KERN_PROC_PGRP:
            /* could do this by traversing pgrp */
            if (p->p_pgrp != (pid_t)name[1])
                continue;
            break;

        case KERN_PROC_TTY:
            fill_from_u(p, &ruid, &ttyp, &ttyd, NULL, 0);
            if (!ttyp || ttyd != (dev_t)name[1])
                continue;
            break;

        case KERN_PROC_UID:
            if (p->p_uid != (uid_t)name[1])
                continue;
            break;

        case KERN_PROC_RUID:
            fill_from_u(p, &ruid, &ttyp, &ttyd, NULL, 0);
            if (ruid != (uid_t)name[1])
                continue;
            break;

        case KERN_PROC_ALL:
            break;
        default:
            return(EINVAL);
        }
        if (buflen >= sizeof(struct kinfo_proc)) {
            bzero((caddr_t)&kproc, sizeof(kproc));
            kproc.kp_proc = *p;
            fill_eproc(p, &kproc.kp_eproc, kproc.ki_comm,
                sizeof(kproc.ki_comm));
            up = p->p_stat != SZOMB ? p->p_uarea : NULL;
            if (up != NULL) {
                kproc.ki_utime = up->u_ru.ru_utime;
                kproc.ki_stime = up->u_ru.ru_stime;
                kproc.ki_cutime = up->u_cru.ru_utime;
                kproc.ki_cstime = up->u_cru.ru_stime;
                kproc.ki_sigs =
                    (up->u_signal[SIGINT] == SIG_IGN) +
                    2 * ((unsigned)up->u_signal[SIGINT] >
                        (unsigned)SIG_IGN) +
                    3 * (up->u_signal[SIGQUIT] == SIG_IGN) +
                    6 * ((unsigned)up->u_signal[SIGQUIT] >
                        (unsigned)SIG_IGN);
            }
            error = copyout((caddr_t)&kproc, (caddr_t)dp,
                sizeof(kproc));
            if (error)
                return (error);
            dp++;
            buflen -= sizeof(struct kinfo_proc);
        }
        needed += sizeof(struct kinfo_proc);
    }
    if (doingzomb == 0) {
        p = zombproc;
        doingzomb++;
        goto again;
    }
    if (where != NULL) {
        *sizep = (caddr_t)dp - where;
        if (needed > *sizep)
            return (ENOMEM);
    } else {
        needed += KERN_PROCSLOP;
        *sizep = needed;
    }
    return (0);
}
