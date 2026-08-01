/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 * Derived from 2.11BSD route(8).  This first RetroBSD/MIPS pass keeps
 * the original SIOCADDRT/SIOCDELRT control path but limits user input
 * to numeric AF_INET addresses.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/route.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <errno.h>

#ifndef INADDR_NONE
#define INADDR_NONE ((u_long)-1)
#endif

#define C(x)    ((unsigned)((x) & 0xff))

struct rtentry route;
int s;
int forcehost, forcenet, doflush, nflag;

main(argc, argv)
	int argc;
	char *argv[];
{

	if (argc < 2) {
		printf("usage: route [ -n ] [ -f ] [ cmd [ net | host ] args ]\n");
		exit(1);
	}
	s = socket(AF_INET, SOCK_RAW, 0);
	if (s < 0) {
		perror("route: socket");
		exit(1);
	}
	argc--, argv++;
	for (; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
		for (argv[0]++; *argv[0]; argv[0]++)
			switch (*argv[0]) {
			case 'f':
				doflush++;
				break;
			case 'n':
				nflag++;
				break;
			default:
				printf("route: unknown option -%c\n", *argv[0]);
				exit(1);
			}
	}
	if (doflush) {
		printf("route: -f is not supported yet\n");
		exit(1);
	}
	if (argc > 0) {
		if (strcmp(*argv, "add") == 0)
			newroute(argc, argv);
		else if (strcmp(*argv, "delete") == 0)
			newroute(argc, argv);
		else if (strcmp(*argv, "change") == 0)
			changeroute(argc - 1, argv + 1);
		else
			printf("%s: huh?\n", *argv);
	}
	exit(0);
}

char *
routename(sa)
	struct sockaddr *sa;
{
	static char line[50];
	struct in_addr in;
	u_short *s;

	switch (sa->sa_family) {
	case AF_INET:
		in = ((struct sockaddr_in *)sa)->sin_addr;
		if (in.s_addr == INADDR_ANY) {
			strcpy(line, "default");
		} else {
			in.s_addr = ntohl(in.s_addr);
			(void)sprintf(line, "%u.%u.%u.%u",
			    C(in.s_addr >> 24), C(in.s_addr >> 16),
			    C(in.s_addr >> 8), C(in.s_addr));
		}
		break;
	default:
		s = (u_short *)sa->sa_data;
		(void)sprintf(line, "af %d: %x %x %x %x %x %x %x",
		    sa->sa_family, s[0], s[1], s[2], s[3], s[4], s[5],
		    s[6]);
		break;
	}
	return (line);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *
netname(sa)
	struct sockaddr *sa;
{
	static char line[50];
	struct in_addr in;
	u_short *s;

	switch (sa->sa_family) {
	case AF_INET:
		in = ((struct sockaddr_in *)sa)->sin_addr;
		in.s_addr = ntohl(in.s_addr);
		if (in.s_addr == 0)
			strcpy(line, "default");
		else if ((in.s_addr & 0xffffff) == 0)
			(void)sprintf(line, "%u", C(in.s_addr >> 24));
		else if ((in.s_addr & 0xffffL) == 0)
			(void)sprintf(line, "%u.%u", C(in.s_addr >> 24),
			    C(in.s_addr >> 16));
		else if ((in.s_addr & 0xff) == 0)
			(void)sprintf(line, "%u.%u.%u", C(in.s_addr >> 24),
			    C(in.s_addr >> 16), C(in.s_addr >> 8));
		else
			(void)sprintf(line, "%u.%u.%u.%u",
			    C(in.s_addr >> 24), C(in.s_addr >> 16),
			    C(in.s_addr >> 8), C(in.s_addr));
		break;
	default:
		s = (u_short *)sa->sa_data;
		(void)sprintf(line, "af %d: %x %x %x %x %x %x %x",
		    sa->sa_family, s[0], s[1], s[2], s[3], s[4], s[5],
		    s[6]);
		break;
	}
	return (line);
}

newroute(argc, argv)
	int argc;
	char *argv[];
{
	char *cmd, *dest, *gateway;
	int ishost, metric = 0, ret, oerrno;
	extern int errno;

	cmd = argv[0];
	if ((strcmp(argv[1], "host")) == 0) {
		forcehost++;
		argc--, argv++;
	} else if ((strcmp(argv[1], "net")) == 0) {
		forcenet++;
		argc--, argv++;
	}
	if (*cmd == 'a') {
		if (argc != 4) {
			printf("usage: %s destination gateway metric\n", cmd);
			printf("(metric of 0 if gateway is this host)\n");
			return;
		}
		metric = atoi(argv[3]);
	} else {
		if (argc < 3) {
			printf("usage: %s destination gateway\n", cmd);
			return;
		}
	}
	ishost = getaddr(argv[1], &route.rt_dst, &dest, forcenet);
	if (forcehost)
		ishost = 1;
	if (forcenet)
		ishost = 0;
	(void)getaddr(argv[2], &route.rt_gateway, &gateway, 0);
	route.rt_flags = RTF_UP;
	if (ishost)
		route.rt_flags |= RTF_HOST;
	if (metric > 0)
		route.rt_flags |= RTF_GATEWAY;

	errno = 0;
	ret = ioctl(s, *cmd == 'a' ? SIOCADDRT : SIOCDELRT,
	    (caddr_t)&route);
	oerrno = errno;
	printf("%s %s %s: gateway %s", cmd, ishost ? "host" : "net",
	    dest, gateway);
	if (ret == 0)
		printf("\n");
	else {
		printf(": ");
		fflush(stdout);
		errno = oerrno;
		error(0);
	}
}

changeroute(argc, argv)
	int argc;
	char *argv[];
{

	printf("not supported\n");
}

error(cmd)
	char *cmd;
{
	extern int errno;

	switch (errno) {
	case ESRCH:
		fprintf(stderr, "not in table\n");
		break;
	case EBUSY:
		fprintf(stderr, "entry in use\n");
		break;
	case ENOBUFS:
		fprintf(stderr, "routing table overflow\n");
		break;
	default:
		perror(cmd);
	}
}

/*
 * Interpret an argument as a numeric AF_INET address, returning 1 if a
 * host address, 0 if a network address.
 */
getaddr(s, sin, name, isnet)
	char *s;
	struct sockaddr_in *sin;
	char **name;
	int isnet;
{
	u_long val;

	bzero((caddr_t)sin, sizeof(*sin));
	if (strcmp(s, "default") == 0) {
		sin->sin_family = AF_INET;
		sin->sin_addr = inet_makeaddr(0L, INADDR_ANY);
		*name = "default";
		return (0);
	}
	sin->sin_family = AF_INET;
	if (isnet == 0) {
		val = inet_addr(s);
		if (val != INADDR_NONE) {
			sin->sin_addr.s_addr = val;
			*name = s;
			return (inet_lnaof(sin->sin_addr) != INADDR_ANY);
		}
	}
	val = inet_network(s);
	if (val != INADDR_NONE) {
		sin->sin_addr = inet_makeaddr(val, INADDR_ANY);
		*name = s;
		return (0);
	}
	fprintf(stderr, "%s: bad inet address\n", s);
	exit(1);
}
