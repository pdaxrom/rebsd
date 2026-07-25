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

int
bcmp(const void *left_arg, const void *right_arg, size_t size)
{
    const unsigned char *left;
    const unsigned char *right;

    left = (const unsigned char *)left_arg;
    right = (const unsigned char *)right_arg;
    while (size-- != 0)
        if (*left++ != *right++)
            return 1;
    return 0;
}

size_t
strlen(const char *text)
{
    const char *end;

    end = text;
    while (*end != '\0')
        ++end;
    return (size_t)(end - text);
}

void
insque(void *element_arg, void *predecessor_arg)
{
    void **element;
    void **predecessor;

    element = (void **)element_arg;
    predecessor = (void **)predecessor_arg;
    element[0] = predecessor[0];
    element[1] = predecessor;
    ((void **)predecessor[0])[1] = element;
    predecessor[0] = element;
}

void
remque(void *element_arg)
{
    void **element;

    element = (void **)element_arg;
    ((void **)element[1])[0] = element[0];
    ((void **)element[0])[1] = element[1];
}
