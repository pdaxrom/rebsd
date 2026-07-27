/*
 * Keep the shell and its linked libraries on one storage allocator.
 *
 * The historical shell allocator and libc malloc both grow through brk(2).
 * Letting them own the same break independently corrupts one arena when the
 * other grows or trims it.  These definitions make the shell allocator the
 * single owner while preserving the standard allocation interface expected
 * by libc and libreadline.
 */
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

#undef free

struct shell_allocation {
    size_t size;
};

void *
malloc(size_t size)
{
    struct shell_allocation *allocation;

    if (size == 0 || size > UINT_MAX - sizeof(*allocation))
        return NULL;
    allocation = (struct shell_allocation *)
        alloc((unsigned)(sizeof(*allocation) + size));
    allocation->size = size;
    return allocation + 1;
}

void
free(void *pointer)
{
    struct shell_allocation *allocation;

    if (pointer == NULL)
        return;
    allocation = (struct shell_allocation *)pointer - 1;
    afree(allocation);
}

void *
calloc(size_t count, size_t size)
{
    void *pointer;
    size_t total;

    if (count != 0 && size > UINT_MAX / count)
        return NULL;
    total = count * size;
    pointer = malloc(total);
    if (pointer != NULL)
        memset(pointer, 0, total);
    return pointer;
}

void *
realloc(void *pointer, size_t size)
{
    struct shell_allocation *allocation;
    void *replacement;
    size_t copy_size;

    if (pointer == NULL)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    allocation = (struct shell_allocation *)pointer - 1;
    replacement = malloc(size);
    if (replacement == NULL)
        return NULL;
    copy_size = allocation->size < size ? allocation->size : size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}
