#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <errno.h>
#include <stdio.h>

extern int h_errno;

static void
print_addr(p)
    unsigned char *p;
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static void
print_bytes(buf, len)
    unsigned char *buf;
    int len;
{
    int i;

    for (i = 0; i < len; i++) {
        printf("%02x%c", ((unsigned int)buf[i] & 0xff),
            ((i & 15) == 15 || i == len - 1) ? '\n' : ' ');
    }
}

static int
check_host(name)
    char *name;
{
    struct hostent *hp;

    hp = gethostbyname(name);
    if (hp == 0) {
        fprintf(stderr, "dns-smoke: gethostbyname %s failed h_errno=%d\n",
            name, h_errno);
        return 1;
    }
    if (hp->h_addrtype != AF_INET || hp->h_length != 4 || hp->h_addr == 0) {
        fprintf(stderr, "dns-smoke: invalid hostent for %s\n", name);
        return 1;
    }
    printf("%s ", hp->h_name);
    print_addr((unsigned char *)hp->h_addr);
    printf("\n");
    return 0;
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    unsigned char *ns;
    unsigned char query[PACKETSZ];
    unsigned char answer[PACKETSZ];
    char *name;
    int ri;
    int qlen;
    int alen;
    int plen;

    name = argc > 1 ? argv[1] : "qemu-gw";
    if (check_host(name))
        return 1;
    if (argc <= 2)
        return 0;
    name = argv[2];

    ri = res_init();
    ns = (unsigned char *)&_res.nsaddr.sin_addr.s_addr;
    printf("resolver: init=%d nscount=%d ns=", ri, _res.nscount);
    print_addr(ns);
    printf(" port=%u options=0x%lx\n",
        (unsigned)ntohs(_res.nsaddr.sin_port), _res.options);
    qlen = res_mkquery(QUERY, name, C_IN, T_A, 0, 0, 0,
        (char *)query, sizeof(query));
    printf("resolver: query len=%d\n", qlen);
    if (qlen > 0)
        print_bytes(query, qlen);
    errno = 0;
    alen = res_send((char *)query, qlen, (char *)answer, sizeof(answer));
    printf("resolver: send answer len=%d errno=%d h_errno=%d\n",
        alen, errno, h_errno);
    if (alen > 0) {
        printf("resolver: rcode=%d ancount=%u\n",
            answer[3] & 15, ((unsigned)answer[6] << 8) | answer[7]);
        plen = alen;
        if (plen > 64)
            plen = 64;
        print_bytes(answer, plen);
    }
    return 0;
}
