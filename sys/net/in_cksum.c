#include <sys/param.h>

#ifdef INET
#include <sys/mbuf.h>

int
in_cksum(struct mbuf *m, int len)
{
    unsigned long sum = 0;
    int odd = 0;
    unsigned char saved = 0;

    for (; m != 0 && len > 0; m = m->m_next) {
        unsigned char *p = (unsigned char *)mtod(m, caddr_t);
        int n = m->m_len;

        if (n > len)
            n = len;
        len -= n;

        if (odd && n > 0) {
            sum += ((unsigned)saved << 8) | *p++;
            n--;
            odd = 0;
        }

        while (n >= 2) {
            sum += ((unsigned)p[0] << 8) | p[1];
            p += 2;
            n -= 2;
        }

        if (n > 0) {
            saved = *p;
            odd = 1;
        }

        while (sum >> 16)
            sum = (sum & 0xffff) + (sum >> 16);
    }

    if (odd)
        sum += (unsigned)saved << 8;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    sum = ~sum & 0xffff;
#if ENDIAN == LITTLE
    sum = ((sum & 0x00ff) << 8) | ((sum & 0xff00) >> 8);
#endif
    return (int)sum;
}
#endif
