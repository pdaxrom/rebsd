/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <string.h>

void *
memmove(void *dst, const void *src, size_t length)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d == s || length == 0)
		return dst;

	if (d < s) {
		do
			*d++ = *s++;
		while (--length);
	} else {
		d += length;
		s += length;
		do
			*--d = *--s;
		while (--length);
	}
	return dst;
}
