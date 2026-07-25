#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <vm/vm_param.h>

#define COPYSTR_CHUNK 64u

static int
copystr_internal(caddr_t source, caddr_t destination, u_int maxlength,
    u_int *lencopied, int source_user)
{
    caddr_t destination_start;
    unsigned char buffer[COPYSTR_CHUNK];
    unsigned chunk;
    unsigned copied;
    unsigned page_left;
    unsigned i;
    int error;
    int terminated;

    destination_start = destination;
    error = ENOENT;
    while (maxlength != 0) {
        chunk = maxlength < COPYSTR_CHUNK ? maxlength : COPYSTR_CHUNK;
        if (source_user) {
            page_left = VM_PAGE_SIZE -
                ((unsigned long)source & VM_PAGE_MASK);
            if (chunk > page_left)
                chunk = page_left;
            error = copyin(source, (caddr_t)buffer, chunk);
            if (error != 0)
                goto done;
        } else {
            for (i = 0; i < chunk; ++i) {
                buffer[i] = ((unsigned char *)source)[i];
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
        bcopy((caddr_t)buffer, destination, copied);
        source += copied;
        destination += copied;
        maxlength -= copied;
        if (terminated) {
            error = 0;
            goto done;
        }
    }
    error = ENOENT;
done:
    if (lencopied != 0)
        *lencopied = destination - destination_start;
    return error;
}

int
copyinstr(caddr_t source, caddr_t destination, u_int maxlength,
    u_int *lencopied)
{
    return copystr_internal(source, destination, maxlength, lencopied, 1);
}

int
copykstr(caddr_t source, caddr_t destination, u_int maxlength,
    u_int *lencopied)
{
    return copystr_internal(source, destination, maxlength, lencopied, 0);
}

int
copystr(caddr_t source, caddr_t destination, u_int maxlength,
    u_int *lencopied)
{
    return copykstr(source, destination, maxlength, lencopied);
}

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
