#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <vm/vm_param.h>

#define COPYSTR_CHUNK 64u

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
    unsigned char buffer[COPYSTR_CHUNK];
    unsigned chunk;
    unsigned copied;
    unsigned page_left;
    unsigned i;
    int error = ENOENT;
    int terminated;
    int src_user;
    int dest_user;

    src_user = (unsigned)src < 0x80000000u;
    dest_user = (unsigned)dest < 0x80000000u;
    while (maxlength != 0) {
        chunk = maxlength < COPYSTR_CHUNK ? maxlength : COPYSTR_CHUNK;
        if (dest_user) {
            page_left = VM_PAGE_SIZE -
                ((unsigned)dest & VM_PAGE_MASK);
            if (chunk > page_left)
                chunk = page_left;
        }
        if (src_user) {
            /* Do not make a terminating NUL depend on the next user page. */
            page_left = VM_PAGE_SIZE -
                ((unsigned)src & VM_PAGE_MASK);
            if (chunk > page_left)
                chunk = page_left;
            error = copyin(src, (caddr_t)buffer, chunk);
            if (error != 0)
                goto done;
        } else {
            for (i = 0; i < chunk; ++i) {
                buffer[i] = ((unsigned char *)src)[i];
                if (buffer[i] == '\0') {
                    chunk = i + 1;
                    break;
                }
            }
        }
        copied = chunk;
        terminated = 0;
        for (i = 0; i < chunk; ++i) {
            if (buffer[i] == '\0') {
                copied = i + 1;
                terminated = 1;
                break;
            }
        }
        if (dest_user)
            error = copyout((caddr_t)buffer, dest, copied);
        else {
            bcopy((caddr_t)buffer, dest, copied);
            error = 0;
        }
        if (error != 0)
            goto done;
        src += copied;
        dest += copied;
        maxlength -= copied;
        if (terminated) {
            error = 0;
            break;
        }
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
