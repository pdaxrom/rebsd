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
static char sccsid[] = "@(#)unix.c	5.5.1 (2.11BSD GTE) 1/1/94";
#endif

/*
 * Display protocol blocks in the unix domain.
 */
#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#define	KERNEL
#include <sys/file.h>

#include "netstat.h"

extern	int Aflag;
extern	int kmem;
extern	char *calloc();

static void unixdomainpr(struct socket *so, kaddr_t soaddr);

void
unixpr(kaddr_t fileaddr, kaddr_t unixsw)
{
	register struct file *fp;
	struct file *fil, *fileNFILE;
	struct socket sock, *so = &sock;
	kaddr_t proto;

	if (fileaddr == 0) {
		printf("file not in namelist.\n");
		return;
	}
	fil = (struct file *)calloc(NFILE, sizeof (struct file));
	if (fil == (struct file *)0) {
		printf("Out of memory (file table).\n");
		return;
	}
	if (!kread(fileaddr, (char *)fil, NFILE * sizeof(struct file),
	    "file table")) {
		printf("File table read error.\n");
		free((char *)fil);
		return;
	}
	fileNFILE = fil + NFILE;
	for (fp = fil; fp < fileNFILE; fp++) {
		if (fp->f_count == 0 || fp->f_type != DTYPE_SOCKET)
			continue;
		if (!kread(KADDR(fp->f_data), (char *)so, sizeof(*so),
		    "socket"))
			continue;
		proto = KADDR(so->so_proto);
		if (unixsw != 0 &&
		    (proto == unixsw || proto == unixsw + sizeof(struct protosw) ||
		    proto == unixsw + 2 * sizeof(struct protosw)))
			if (so->so_pcb)
				unixdomainpr(so, KADDR(fp->f_data));
	}
	free((char *)fil);
}

static	char *socktype[] =
    { "#0", "stream", "dgram", "raw", "rdm", "seqpacket" };

static void
unixdomainpr(struct socket *so, kaddr_t soaddr)
{
	struct unpcb unpcb, *unp = &unpcb;
	struct mbuf mbuf, *m;
	struct sockaddr_un *sa = (struct sockaddr_un *)0;
	int pathlen;
	static int first = 1;
#ifdef pdp11
#define klseek slseek
#endif

	if (!kread(KADDR(so->so_pcb), (char *)unp, sizeof(*unp),
	    "UNIX-domain control block"))
		return;
	if (unp->unp_addr) {
		m = &mbuf;
		if (!kread(KADDR(unp->unp_addr), (char *)m, sizeof(*m),
		    "UNIX-domain address")) {
			m = (struct mbuf *)0;
		} else {
			sa = mtod(m, struct sockaddr_un *);
			if ((char *)sa < (char *)&mbuf ||
			    (char *)sa + sizeof(sa->sun_family) >
			    (char *)(&mbuf + 1)) {
				m = (struct mbuf *)0;
				sa = (struct sockaddr_un *)0;
			}
		}
	} else
		m = (struct mbuf *)0;
	if (first) {
		printf("Active UNIX domain sockets\n");
		printf(
"%-8.8s %-6.6s %-6.6s %-6.6s %8.8s %8.8s %8.8s %8.8s Addr\n",
		    "Address", "Type", "Recv-Q", "Send-Q",
		    "Inode", "Conn", "Refs", "Nextref");
		first = 0;
	}
	printf("%8x %-6.6s %6d %6d %8x %8x %8x %8x",
	    soaddr,
	    so->so_type >= 0 && so->so_type < sizeof(socktype) / sizeof(socktype[0])
	    ? socktype[so->so_type] : "unknown",
	    so->so_rcv.sb_cc, so->so_snd.sb_cc,
	    unp->unp_inode, unp->unp_conn,
	    unp->unp_refs, unp->unp_nextref);
	if (m && m->m_len > sizeof(sa->sun_family)) {
		pathlen = m->m_len - sizeof(sa->sun_family);
		if (pathlen > sizeof(sa->sun_path))
			pathlen = sizeof(sa->sun_path);
		if ((char *)sa->sun_path + pathlen > (char *)(&mbuf + 1))
			pathlen = (char *)(&mbuf + 1) - (char *)sa->sun_path;
		if (pathlen > 0)
			printf(" %.*s", pathlen, sa->sun_path);
	}
	putchar('\n');
}
