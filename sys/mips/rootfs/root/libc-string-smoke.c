#include <stdio.h>

static char pattern[64];
static char expect[64];
static char got[64];

static void
fill_pattern()
{
    int i;

    for (i = 0; i < sizeof(pattern); i++)
        pattern[i] = (char)(0x40 + i);
}

static void
clear_buf(p)
    char *p;
{
    int i;

    for (i = 0; i < sizeof(got); i++)
        p[i] = (char)0xa5;
}

static int
check_buf(tag, p, q, len)
    char *tag;
    char *p;
    char *q;
    int len;
{
    int i;

    for (i = 0; i < len; i++) {
        if (p[i] != q[i]) {
            printf("%s mismatch at %d: got=%x expect=%x\n", tag, i,
                (unsigned int)p[i] & 0xff, (unsigned int)q[i] & 0xff);
            return 1;
        }
    }
    return 0;
}

static int
check_copy()
{
    int so, doff, len, i;

    for (so = 0; so < 4; so++) {
        for (doff = 0; doff < 4; doff++) {
            for (len = 0; len <= 24; len++) {
                clear_buf(expect);
                clear_buf(got);
                for (i = 0; i < len; i++)
                    expect[doff + i] = pattern[so + i];
                bcopy(pattern + so, got + doff, len);
                if (check_buf("bcopy", got, expect, sizeof(got)))
                    return 1;

                clear_buf(got);
                memcpy(got + doff, pattern + so, len);
                if (check_buf("memcpy", got, expect, sizeof(got)))
                    return 1;

                clear_buf(got);
                memmove(got + doff, pattern + so, len);
                if (check_buf("memmove", got, expect, sizeof(got)))
                    return 1;
            }
        }
    }
    return 0;
}

static int
check_overlap()
{
    int off, len, i;
    char m[64], e[64];

    for (off = 1; off < 8; off++) {
        for (len = 1; len <= 24; len++) {
            for (i = 0; i < sizeof(m); i++)
                m[i] = e[i] = pattern[i];
            for (i = len - 1; i >= 0; i--)
                e[off + i] = e[i];
            memmove(m + off, m, len);
            if (check_buf("memmove overlap", m, e, sizeof(m)))
                return 1;
        }
    }
    return 0;
}

static int
check_compare()
{
    int so, doff, len;

    for (so = 0; so < 4; so++) {
        for (doff = 0; doff < 4; doff++) {
            for (len = 0; len <= 24; len++) {
                if (bcmp(pattern + so, pattern + so, len) != 0) {
                    printf("bcmp equal failed so=%d len=%d\n", so, len);
                    return 1;
                }
                bcopy(pattern + so, got + doff, len);
                if (bcmp(pattern + so, got + doff, len) != 0) {
                    printf("bcmp copied failed so=%d do=%d len=%d\n",
                        so, doff, len);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int
check_fill()
{
    int off, len, i;

    for (off = 0; off < 4; off++) {
        for (len = 0; len <= 24; len++) {
            clear_buf(expect);
            clear_buf(got);
            for (i = 0; i < len; i++)
                expect[off + i] = 0;
            bzero(got + off, len);
            if (check_buf("bzero", got, expect, sizeof(got)))
                return 1;

            clear_buf(expect);
            clear_buf(got);
            for (i = 0; i < len; i++)
                expect[off + i] = 0x5a;
            memset(got + off, 0x5a, len);
            if (check_buf("memset", got, expect, sizeof(got)))
                return 1;
        }
    }
    return 0;
}

int
main()
{
    fill_pattern();
    if (check_copy())
        return 1;
    if (check_overlap())
        return 1;
    if (check_compare())
        return 1;
    if (check_fill())
        return 1;
    printf("libc string smoke ok\n");
    return 0;
}
