/*
 * Copyright (c) 1983,1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1983 Regents of the University of California.\n\
 All rights reserved.\n";
#endif

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <ctype.h>
#include <nlist.h>
#include <stdio.h>

struct nlist nl[] = {
#define N_MBSTAT       0
    { "_mbstat" },
#define N_IPSTAT       1
    { "_ipstat" },
#define N_TCB          2
    { "_tcb" },
#define N_TCPSTAT      3
    { "_tcpstat" },
#define N_UDB          4
    { "_udb" },
#define N_UDPSTAT      5
    { "_udpstat" },
#define N_RAWCB        6
    { "_rawcb" },
#define N_IFNET        7
    { "_ifnet" },
#define N_RTHOST       8
    { "_rthost" },
#define N_RTNET        9
    { "_rtnet" },
#define N_ICMPSTAT     10
    { "_icmpstat" },
#define N_RTSTAT       11
    { "_rtstat" },
#define N_FILE         12
    { "_file" },
#define N_UNIXSW       13
    { "_unixsw" },
#define N_RTHASHSIZE   14
    { "_rthashsize" },
    { 0 },
};

extern int protopr();
extern int tcp_stats(), udp_stats(), ip_stats(), icmp_stats();

#define NULLPROTOX ((struct protox *)0)
struct protox {
    u_char  pr_index;
    u_char  pr_sindex;
    u_char  pr_wanted;
    int     (*pr_cblocks)();
    int     (*pr_stats)();
    char    *pr_name;
} protox[] = {
    { N_TCB,       N_TCPSTAT,    1, protopr, tcp_stats,  "tcp"  },
    { N_UDB,       N_UDPSTAT,    1, protopr, udp_stats,  "udp"  },
    { -1,          N_IPSTAT,     1, 0,       ip_stats,   "ip"   },
    { -1,          N_ICMPSTAT,   1, 0,       icmp_stats, "icmp" },
    { -1,          -1,           0, 0,       0,          0      },
};

char    *kmemf = "/dev/kmem";
int     kmem;
int     Aflag;
int     aflag;
int     iflag;
int     mflag;
int     nflag = 1;        /* RetroBSD/MIPS rootfs has no network database yet. */
int     pflag;
int     rflag;
int     sflag;
int     tflag;
int     interval;
char    *interface;
int     unit;
char    usage[] = "[ -Aaimnrstu ] [-f inet|unix] [-p proto] [-I interface] [ interval ]";

int     af = AF_UNSPEC;

extern off_t lseek();

static struct protox *knownname();
static void usage_exit();

main(argc, argv)
    int argc;
    char *argv[];
{
    char *cp = 0, *name;
    register struct protox *tp = NULLPROTOX;
    struct protox *name2protox();

    name = argv[0];
    argc--, argv++;
    while (argc > 0 && **argv == '-') {
        for (cp = &argv[0][1]; *cp; cp++)
        switch (*cp) {
        case 'A':
            Aflag++;
            break;
        case 'a':
            aflag++;
            break;
        case 'i':
            iflag++;
            break;
        case 'm':
            mflag++;
            break;
        case 'n':
            nflag++;
            break;
        case 'r':
            rflag++;
            break;
        case 's':
            sflag++;
            break;
        case 't':
            tflag++;
            break;
        case 'u':
            af = AF_UNIX;
            break;
        case 'p':
            argv++;
            argc--;
            if (argc == 0)
                usage_exit(name);
            if ((tp = name2protox(*argv)) == NULLPROTOX) {
                fprintf(stderr, "%s: unknown protocol\n", *argv);
                exit(10);
            }
            pflag++;
            break;
        case 'f':
            argv++;
            argc--;
            if (argc == 0)
                usage_exit(name);
            if (strcmp(*argv, "inet") == 0)
                af = AF_INET;
            else if (strcmp(*argv, "unix") == 0)
                af = AF_UNIX;
            else {
                fprintf(stderr, "%s: unknown address family\n", *argv);
                exit(10);
            }
            break;
        case 'I':
            iflag++;
            if (*(interface = cp + 1) == 0) {
                if ((interface = argv[1]) == 0)
                    break;
                argv++;
                argc--;
            }
            for (cp = interface; isalpha(*cp); cp++)
                ;
            unit = atoi(cp);
            *cp-- = 0;
            break;
        default:
            usage_exit(name);
        }
        argv++, argc--;
    }
    if (argc > 0 && isdigit(argv[0][0])) {
        interval = atoi(argv[0]);
        if (interval <= 0)
            usage_exit(name);
        argv++, argc--;
        iflag++;
    }
    if (argc != 0)
        usage_exit(name);

    knlist(nl);
    if (nl[0].n_type == 0) {
        fprintf(stderr, "no kernel namelist\n");
        exit(1);
    }
    kmem = open(kmemf, 0);
    if (kmem < 0) {
        fprintf(stderr, "cannot open ");
        perror(kmemf);
        exit(1);
    }

    if (mflag) {
        mbpr((off_t)nl[N_MBSTAT].n_value);
        exit(0);
    }
    if (pflag) {
        if (tp->pr_stats)
            (*tp->pr_stats)(nl[tp->pr_sindex].n_value, tp->pr_name);
        else
            printf("%s: no stats routine\n", tp->pr_name);
        exit(0);
    }
    if (iflag) {
        intpr(interval, nl[N_IFNET].n_value);
        exit(0);
    }
    if (rflag) {
        if (sflag)
            rt_stats((off_t)nl[N_RTSTAT].n_value);
        else
            routepr((off_t)nl[N_RTHOST].n_value,
                (off_t)nl[N_RTNET].n_value,
                (off_t)nl[N_RTHASHSIZE].n_value);
        exit(0);
    }

    if (af == AF_INET || af == AF_UNSPEC) {
        for (tp = protox; tp->pr_name; tp++) {
            if (sflag) {
                if (tp->pr_stats)
                    (*tp->pr_stats)(nl[tp->pr_sindex].n_value,
                        tp->pr_name);
            } else if (tp->pr_cblocks) {
                (*tp->pr_cblocks)(nl[tp->pr_index].n_value,
                    tp->pr_name);
            }
        }
    }
    if ((af == AF_UNIX || af == AF_UNSPEC) && !sflag)
        unixpr((off_t)nl[N_FILE].n_value,
            (struct protosw *)nl[N_UNIXSW].n_value);
    exit(0);
}

static void
usage_exit(name)
    char *name;
{
    printf("usage: %s %s\n", name, usage);
    exit(1);
}

/*
 * Seek into live kernel memory.  MIPS /dev/kmem accepts kernel virtual
 * addresses directly.
 */
off_t
klseek(fd, base, off)
    int fd, off;
    off_t base;
{
    return lseek(fd, base, off);
}

char *
plural(n)
    int n;
{
    return n != 1 ? "s" : "";
}

static struct protox *
knownname(name)
    char *name;
{
    struct protox *tp;

    for (tp = protox; tp->pr_name; tp++)
        if (strcmp(tp->pr_name, name) == 0)
            return tp;
    return NULLPROTOX;
}

struct protox *
name2protox(name)
    char *name;
{
    return knownname(name);
}
