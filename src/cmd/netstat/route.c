/*
 * Copyright (c) 1983,1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#if	defined(DOSCCS) && !defined(lint)
static char sccsid[] = "@(#)route.c	5.13.1 (2.11BSD GTE) 1/1/94";
#endif

#include <stdio.h>
#include <strings.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>

#ifdef pdp11
#define klseek slseek
#endif

extern	int kmem;
extern	int nflag;
extern	char *routename(), *netname(), *plural();
extern	char *malloc();

#define C(x)	(u_char)((x) & 0xff)

/*
 * Definitions for showing gateway flags.
 */
struct bits {
	short	b_mask;
	char	b_val;
} bits[] = {
	{ RTF_UP,	'U' },
	{ RTF_GATEWAY,	'G' },
	{ RTF_HOST,	'H' },
	{ RTF_DYNAMIC,	'D' },
	{ RTF_MODIFIED,	'M' },
	{ 0 }
};

/*
 * Print routing tables.
 */
routepr(hostaddr, netaddr, hashsizeaddr)
	off_t hostaddr, netaddr, hashsizeaddr;
{
	struct mbuf mb;
	register struct rtentry *rt;
	register struct mbuf *m;
	register struct bits *p;
	char name[16], *flags;
	struct mbuf **routehash;
	struct ifnet ifnet;
	int hashsize;
	int i, doinghost = 1;

	if (hostaddr == 0) {
		printf("rthost: symbol not in namelist\n");
		return;
	}
	if (netaddr == 0) {
		printf("rtnet: symbol not in namelist\n");
		return;
	}
	if (hashsizeaddr == 0) {
		printf("rthashsize: symbol not in namelist\n");
		return;
	}
	klseek(kmem, hashsizeaddr, 0);
	read(kmem, (char *)&hashsize, sizeof (hashsize));
	routehash = (struct mbuf **)malloc( hashsize*sizeof (struct mbuf *) );
	klseek(kmem, hostaddr, 0);
	read(kmem, (char *)routehash, hashsize*sizeof (struct mbuf *));
	printf("Routing tables\n");
	printf("%-16.16s %-18.18s %-6.6s  %6.6s%8.8s  %s\n",
		"Destination", "Gateway",
		"Flags", "Refs", "Use", "Interface");
again:
	for (i = 0; i < hashsize; i++) {
		if (routehash[i] == 0)
			continue;
		m = routehash[i];
		while (m) {
			struct sockaddr_in *sin;

			klseek(kmem, (off_t)m, 0);
			read(kmem, (char *)&mb, sizeof (mb));
			rt = mtod(&mb, struct rtentry *);
			if ((unsigned)rt < (unsigned)&mb ||
			    (unsigned)rt >= (unsigned)(&mb + 1)) {
				printf("???\n");
				return;
			}

			switch(rt->rt_dst.sa_family) {
			case AF_INET:
				sin = (struct sockaddr_in *)&rt->rt_dst;
				printf("%-16.16s ",
				    (sin->sin_addr.s_addr == 0) ? "default" :
				    (rt->rt_flags & RTF_HOST) ?
				    routename(sin->sin_addr) :
					netname(sin->sin_addr, 0L));
				sin = (struct sockaddr_in *)&rt->rt_gateway;
				printf("%-18.18s ", routename(sin->sin_addr));
				break;
			default:
				{
				u_short *s = (u_short *)rt->rt_dst.sa_data;
				printf("(%d)%x %x %x %x %x %x %x ",
				    rt->rt_dst.sa_family,
				    s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
				s = (u_short *)rt->rt_gateway.sa_data;
				printf("(%d)%x %x %x %x %x %x %x ",
				    rt->rt_gateway.sa_family,
				    s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
				}
			}
			for (flags = name, p = bits; p->b_mask; p++)
				if (p->b_mask & rt->rt_flags)
					*flags++ = p->b_val;
			*flags = '\0';
			printf("%-6.6s %6d %8ld ", name,
				rt->rt_refcnt, rt->rt_use);
			if (rt->rt_ifp == 0) {
				putchar('\n');
				m = mb.m_next;
				continue;
			}
			klseek(kmem, (off_t)rt->rt_ifp, 0);
			read(kmem, (char *)&ifnet, sizeof (ifnet));
			klseek(kmem, (off_t)ifnet.if_name, 0);
			read(kmem, name, 16);
			printf(" %.15s%d\n", name, ifnet.if_unit);
			m = mb.m_next;
		}
	}
	if (doinghost) {
		klseek(kmem, netaddr, 0);
		read(kmem, (char *)routehash, hashsize*sizeof (struct mbuf *));
		doinghost = 0;
		goto again;
	}
	free((char *)routehash);
}

char *
routename(in)
	struct in_addr in;
{
	static char line[MAXHOSTNAMELEN + 1];

	in.s_addr = ntohl(in.s_addr);
	sprintf(line, "%u.%u.%u.%u", C(in.s_addr >> 24),
		C(in.s_addr >> 16), C(in.s_addr >> 8), C(in.s_addr));
	return (line);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *
netname(in, mask)
	struct in_addr in;
	u_long mask;
{
	static char line[MAXHOSTNAMELEN + 1];
	long i;

	i = ntohl(in.s_addr);
	if ((i & 0xffffffL) == 0)
		sprintf(line, "%u", C(i >> 24));
	else if ((i & 0xffffL) == 0)
		sprintf(line, "%u.%u", C(i >> 24) , C(i >> 16));
	else if ((i & 0xffL) == 0)
		sprintf(line, "%u.%u.%u", C(i >> 24), C(i >> 16), C(i >> 8));
	else
		sprintf(line, "%u.%u.%u.%u", C(i >> 24),
			C(i >> 16), C(i >> 8), C(i));
	return (line);
}

/*
 * Print routing statistics
 */
rt_stats(off)
	off_t off;
{
	struct rtstat rtstat;

	if (off == 0) {
		printf("rtstat: symbol not in namelist\n");
		return;
	}
	klseek(kmem, off, 0);
	read(kmem, (char *)&rtstat, sizeof (rtstat));
	printf("routing:\n");
	printf("\t%u bad routing redirect%s\n",
		rtstat.rts_badredirect, plural((long)rtstat.rts_badredirect));
	printf("\t%u dynamically created route%s\n",
		rtstat.rts_dynamic, plural((long)rtstat.rts_dynamic));
	printf("\t%u new gateway%s due to redirects\n",
		rtstat.rts_newgateway, plural((long)rtstat.rts_newgateway));
	printf("\t%u destination%s found unreachable\n",
		rtstat.rts_unreach, plural((long)rtstat.rts_unreach));
	printf("\t%u use%s of a wildcard route\n",
		rtstat.rts_wildcard, plural((long)rtstat.rts_wildcard));
}
