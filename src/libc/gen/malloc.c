#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Circular first-fit allocator.
 *
 * Every allocation is preceded by an aligned header containing its size in
 * header units.  Free blocks are kept in address order and adjacent blocks
 * are coalesced.  Unlike the historical allocator, realloc() never releases
 * the source before copying it and the arena does not depend on a pointer
 * stored at the current program break.
 */
typedef union malloc_header malloc_header_t;

union malloc_header {
	struct {
		malloc_header_t *next;
		size_t units;
	} free;
	long double align_long_double;
	long long align_long_long;
	void *align_pointer;
};

#define MALLOC_CHUNK_BYTES 1024

static malloc_header_t malloc_base;
static malloc_header_t *malloc_freep;

void free(void *);

static malloc_header_t *
malloc_morecore(size_t units)
{
	malloc_header_t *block;
	size_t minimum;
	size_t bytes;
	void *memory;

	minimum = MALLOC_CHUNK_BYTES / sizeof(*block);
	if (minimum < 2)
		minimum = 2;
	if (units < minimum)
		units = minimum;
	if (units > (size_t)INT_MAX / sizeof(*block)) {
		errno = ENOMEM;
		return NULL;
	}
	bytes = units * sizeof(*block);
	memory = sbrk((int)bytes);
	if (memory == (void *)-1)
		return NULL;
	block = memory;
	block->free.units = units;
	free(block + 1);
	return malloc_freep;
}

void *
malloc(size_t nbytes)
{
	malloc_header_t *block;
	malloc_header_t *previous;
	size_t units;

	if (nbytes == 0)
		return NULL;
	if (nbytes > (size_t)-1 - sizeof(malloc_header_t)) {
		errno = ENOMEM;
		return NULL;
	}
	units = (nbytes + sizeof(malloc_header_t) - 1) /
	    sizeof(malloc_header_t) + 1;

	if (malloc_freep == NULL) {
		malloc_base.free.next = &malloc_base;
		malloc_base.free.units = 0;
		malloc_freep = &malloc_base;
	}
	previous = malloc_freep;
	for (block = previous->free.next; ; previous = block,
	    block = block->free.next) {
		if (block->free.units >= units) {
			if (block->free.units == units) {
				previous->free.next = block->free.next;
			} else {
				block->free.units -= units;
				block += block->free.units;
				block->free.units = units;
			}
			malloc_freep = previous;
			return block + 1;
		}
		if (block == malloc_freep) {
			block = malloc_morecore(units);
			if (block == NULL)
				return NULL;
		}
	}
}

void
free(void *pointer)
{
	malloc_header_t *block;
	malloc_header_t *current;

	if (pointer == NULL)
		return;
	block = (malloc_header_t *)pointer - 1;
	if (malloc_freep == NULL) {
		malloc_base.free.next = &malloc_base;
		malloc_base.free.units = 0;
		malloc_freep = &malloc_base;
	}
	for (current = malloc_freep;
	    !(block > current && block < current->free.next);
	    current = current->free.next) {
		if (current >= current->free.next &&
		    (block > current || block < current->free.next))
			break;
	}
	if (block + block->free.units == current->free.next) {
		block->free.units += current->free.next->free.units;
		block->free.next = current->free.next->free.next;
	} else {
		block->free.next = current->free.next;
	}
	if (current + current->free.units == block) {
		current->free.units += block->free.units;
		current->free.next = block->free.next;
	} else {
		current->free.next = block;
	}
	malloc_freep = current;
}

void *
realloc(void *pointer, size_t nbytes)
{
	malloc_header_t *block;
	unsigned char *replacement;
	size_t oldbytes;
	size_t copybytes;

	if (pointer == NULL)
		return malloc(nbytes);
	if (nbytes == 0) {
		free(pointer);
		return NULL;
	}
	block = (malloc_header_t *)pointer - 1;
	oldbytes = (block->free.units - 1) * sizeof(*block);
	replacement = malloc(nbytes);
	if (replacement == NULL)
		return NULL;
	copybytes = oldbytes < nbytes ? oldbytes : nbytes;
	memcpy(replacement, pointer, copybytes);
	free(pointer);
	return replacement;
}
