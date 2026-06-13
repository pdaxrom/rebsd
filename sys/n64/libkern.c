#include <sys/param.h>
#include <sys/errno.h>

int
ffs(u_long value)
{
    int bit;

    if (value == 0)
        return 0;

    for (bit = 1; (value & 1u) == 0; ++bit)
        value >>= 1;
    return bit;
}

int
copystr(caddr_t src, caddr_t dest, u_int maxlength, u_int *lencopied)
{
    caddr_t dest0 = dest;
    int error = ENOENT;

    if (maxlength != 0) {
        while ((*dest++ = *src++) != '\0') {
            if (--maxlength == 0)
                goto done;
        }
        error = 0;
    }
done:
    if (lencopied != 0)
        *lencopied = dest - dest0;
    return error;
}

size_t
strlen(const char *s)
{
    const char *start = s;

    while (*s != '\0')
        ++s;
    return s - start;
}

void
insque(void *element, void *predecessor)
{
    struct queue {
        struct queue *next;
        struct queue *prev;
    };
    struct queue *e = element;
    struct queue *p = predecessor;

    e->prev = p;
    e->next = p->next;
    p->next->prev = e;
    p->next = e;
}

void
remque(void *element)
{
    struct queue {
        struct queue *next;
        struct queue *prev;
    };
    struct queue *e = element;

    e->prev->next = e->next;
    e->next->prev = e->prev;
}

void
bcopy(const void *src0, void *dst0, size_t nbytes)
{
    const unsigned char *src = src0;
    unsigned char *dst = dst0;

    if (dst > src && dst < src + nbytes) {
        src += nbytes;
        dst += nbytes;
        while (nbytes-- != 0)
            *--dst = *--src;
    } else {
        while (nbytes-- != 0)
            *dst++ = *src++;
    }
}

void *
memcpy(void *dst, const void *src, size_t nbytes)
{
    bcopy(src, dst, nbytes);
    return dst;
}

void *
memmove(void *dst, const void *src, size_t nbytes)
{
    bcopy(src, dst, nbytes);
    return dst;
}

void *
memset(void *dst0, int value, size_t nbytes)
{
    unsigned char *dst = dst0;

    while (nbytes-- != 0)
        *dst++ = value;
    return dst0;
}

char *
strncpy(char *dst, const char *src, size_t nbytes)
{
    char *start = dst;

    while (nbytes != 0 && *src != '\0') {
        *dst++ = *src++;
        nbytes--;
    }
    while (nbytes-- != 0)
        *dst++ = '\0';
    return start;
}

int
strncmp(const char *s1, const char *s2, size_t nbytes)
{
    while (nbytes-- != 0) {
        if (*s1 != *s2)
            return (unsigned char)*s1 - (unsigned char)*s2;
        if (*s1 == '\0')
            return 0;
        s1++;
        s2++;
    }
    return 0;
}

void
bzero(void *dst0, size_t nbytes)
{
    unsigned char *dst = dst0;

    while (nbytes-- != 0)
        *dst++ = 0;
}

int
bcmp(const void *m1, const void *m2, size_t nbytes)
{
    const unsigned char *s1 = m1;
    const unsigned char *s2 = m2;

    while (nbytes-- != 0) {
        if (*s1 != *s2)
            return *s1 - *s2;
        ++s1;
        ++s2;
    }
    return 0;
}
