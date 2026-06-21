/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/*
 * storage allocator
 * (circular first fit strategy)
 */

#define BUSY 01
#define busy(x) (Rcheat((x)->word) & BUSY)

unsigned brkincr = BRKINCR;
struct blk *blokp;   /*current search pointer*/
struct blk *bloktop; /* top of arena (last blok) */

char *brkbegin;

#ifdef TARGET_VR4300
static void
sh_diag_puts(const char *s)
{
    while (*s)
        write(2, s++, 1);
}

static void
sh_diag_hex(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[10];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++)
        buf[2 + i] = digits[(value >> (28 - i * 4)) & 0x0f];
    write(2, buf, sizeof(buf));
}

static int
sh_bad_blk(struct blk *p)
{
    if (p == NIL)
        return 1;
    if (brkbegin == NIL || bloktop == NIL)
        return 0;
    if ((char *)p < brkbegin || p > bloktop)
        return 1;
    if (Rcheat(p) & (BYTESPERWORD - 1))
        return 1;
    return 0;
}

static void
sh_alloc_corrupt(const char *where, struct blk *p, struct blk *q,
    unsigned rbytes)
{
    sh_diag_puts("sh alloc corrupt: ");
    sh_diag_puts(where);
    sh_diag_puts(" brkbegin=");
    sh_diag_hex((unsigned)brkbegin);
    sh_diag_puts(" blokp=");
    sh_diag_hex((unsigned)blokp);
    sh_diag_puts(" bloktop=");
    sh_diag_hex((unsigned)bloktop);
    sh_diag_puts(" p=");
    sh_diag_hex((unsigned)p);
    sh_diag_puts(" q=");
    sh_diag_hex((unsigned)q);
    sh_diag_puts(" rbytes=");
    sh_diag_hex(rbytes);
    sh_diag_puts("\n");
    _exit(125);
}

static void
sh_alloc_check(struct blk *p, const char *where, unsigned rbytes)
{
    if (sh_bad_blk(p))
        sh_alloc_corrupt(where, p, NIL, rbytes);
}
#else
#define sh_alloc_check(p, where, rbytes) ((void)0)
#endif

char *alloc(unsigned nbytes)
{
    register unsigned rbytes = round(nbytes + BYTESPERWORD, BYTESPERWORD);

    for (;;) {
        int c = 0;
        register struct blk *p = blokp;
        register struct blk *q;

        do {
            sh_alloc_check(p, "scan", rbytes);
            if (!busy(p)) {
                q = p->word;
                sh_alloc_check((struct blk *)(Rcheat(q) & ~BUSY),
                    "next", rbytes);
                while (!busy(q)) {
                    p->word = q->word;
                    q = p->word;
                    sh_alloc_check((struct blk *)(Rcheat(q) & ~BUSY),
                        "coalesce", rbytes);
                }
                if ((char *)q - (char *)p >= rbytes) {
                    blokp = (struct blk *)((char *)p + rbytes);
                    if (q > blokp)
                        blokp->word = p->word;
                    p->word = (struct blk *)(Rcheat(blokp) | BUSY);
                    return ((char *)(p + 1));
                }
            }
            q = p;
            p = (struct blk *)(Rcheat(p->word) & ~BUSY);
            sh_alloc_check(p, "advance", rbytes);
        } while (p > q || (c++) == 0);
        addblok(rbytes);
    }
}

void addblok(unsigned reqd)
{
    if (stakbot == NIL) {
        extern int end;
        brkbegin = setbrk(BRKINCR * 5);
        bloktop = (struct blk *)&end;
    }

    if (stakbas != staktop) {
        register char *rndstak;
        register struct blk *blokstak;

        pushstak(0);
        rndstak = (char *)round(staktop, BYTESPERWORD);
        blokstak = (struct blk *)(stakbas)-1;
        blokstak->word = stakbsy;
        stakbsy = blokstak;
        bloktop->word = (struct blk *)(Rcheat(rndstak) | BUSY);
        bloktop = (struct blk *)(rndstak);
    }
    reqd += brkincr;
    reqd &= ~(brkincr - 1);
    blokp = bloktop;
    bloktop = bloktop->word = (struct blk *)(Rcheat(bloktop) + reqd);
    bloktop->word = (struct blk *)(brkbegin + 1);
    {
        register char *stakadr = (char *)(bloktop + 2);

        if (stakbot != staktop)
            staktop = movstr(stakbot, stakadr);
        else
            staktop = stakadr;

        stakbas = stakbot = stakadr;
    }
}

void free(void *ap)
{
    register struct blk *p;

    if ((p = ap) && p < bloktop) {
#ifdef TARGET_VR4300
        if (sh_bad_blk(p) || p <= (struct blk *)brkbegin)
            sh_alloc_corrupt("free-arg", p, NIL, 0);
#endif
#ifdef DEBUG
        chkbptr(p);
#endif
        --p;
#ifdef TARGET_VR4300
        if (sh_bad_blk(p))
            sh_alloc_corrupt("free-head", p, NIL, 0);
#endif
        p->word = (struct blk *)(Rcheat(p->word) & ~BUSY);
    }
}

#ifdef DEBUG
void chkbptr(struct blk *ptr)
{
    int exf = 0;
    register struct blk *p = (struct blk *)brkbegin;
    register struct blk *q;
    int us = 0, un = 0;

    for (;;) {
        q = (struct blk *)(Rcheat(p->word) & ~BUSY);

        if (p + 1 == ptr)
            exf++;

        if (q < (struct blk *)brkbegin || q > bloktop)
            abort(3);

        if (p == bloktop)
            break;

        if (busy(p))
            us += q - p;
        else
            un += q - p;

        if (p >= q)
            abort(4);

        p = q;
    }
    if (exf == 0)
        abort(1);
}

void chkmem()
{
    register struct blk *p = (struct blk *)brkbegin;
    register struct blk *q;
    int us = 0, un = 0;

    for (;;) {
        q = (struct blk *)(Rcheat(p->word) & ~BUSY);

        if (q < (struct blk *)brkbegin || q > bloktop)
            abort(3);

        if (p == bloktop)
            break;

        if (busy(p))
            us += q - p;
        else
            un += q - p;

        if (p >= q)
            abort(4);

        p = q;
    }

    prs("un/used/avail ");
    prn(un);
    blank();
    prn(us);
    blank();
    prn((char *)bloktop - brkbegin - (un + us));
    newline();
}
#endif
