/*
 * Calloc - allocate and clear memory block
 */
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>

void *
calloc(size_t num, size_t size)
{
	register char *p;
	size_t total;

	if (size != 0 && num > (size_t)-1 / size) {
		errno = ENOMEM;
		return NULL;
	}
	total = num * size;
	p = malloc(total);
	if (p)
		bzero(p, total);
	return (p);
}
