#include <errno.h>
#include <stdlib.h>

void *
reallocarray(void *pointer, size_t count, size_t size)
{
    if (size != 0 && count > (size_t)-1 / size) {
        errno = ENOMEM;
        return 0;
    }
    return realloc(pointer, count * size);
}
