#include <sys/types.h>

void
bzero(void *destination, size_t size)
{
    unsigned char *bytes;

    bytes = (unsigned char *)destination;
    while (size-- != 0)
        *bytes++ = 0;
}

void
bcopy(const void *source, void *destination, size_t size)
{
    const unsigned char *from;
    unsigned char *to;

    from = (const unsigned char *)source;
    to = (unsigned char *)destination;
    if (to == from || size == 0)
        return;
    if (to < from) {
        while (size-- != 0)
            *to++ = *from++;
    } else {
        from += size;
        to += size;
        while (size-- != 0)
            *--to = *--from;
    }
}
