/*
 * C library allocator interface for the shell.
 *
 * The shell has its own allocator and temporary stack, both backed by
 * sbrk().  Using libc malloc() in the same process creates a second,
 * independent owner of the program break.  In particular, stakchk() can
 * then release storage which libc still owns.
 *
 * Provide the standard allocation entry points here so libraries linked
 * into sh (readline, stdio, opendir, and others) share the shell arena.
 */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern char *alloc(unsigned nbytes);
extern void afree(void *pointer);

struct heap_header {
    size_t size;
};

void *
malloc(size_t size)
{
    struct heap_header *header;

    if (size == 0)
        return NULL;
    if (size > (size_t)-1 - sizeof(*header) - sizeof(void *)) {
        errno = ENOMEM;
        return NULL;
    }

    header = (struct heap_header *)alloc((unsigned)(sizeof(*header) + size));
    header->size = size;
    return header + 1;
}

void
free(void *pointer)
{
    struct heap_header *header;

    if (pointer == NULL)
        return;
    header = (struct heap_header *)pointer - 1;
    afree(header);
}

void *
realloc(void *pointer, size_t size)
{
    struct heap_header *header;
    void *replacement;
    size_t copy_size;

    if (pointer == NULL)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }

    header = (struct heap_header *)pointer - 1;
    if (size <= header->size) {
        header->size = size;
        return pointer;
    }

    replacement = malloc(size);
    if (replacement == NULL)
        return NULL;
    copy_size = header->size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}
