/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * bcopy -- vax movc3 instruction
 */
void
bcopy(const void *vsrc, void *vdst, unsigned int length)
{
	register const char *src = vsrc;
	register char *dst = vdst;

	if (length && src != dst) {
		if (dst < src) {
			do
				*dst++ = *src++;
			while (--length);
		} else {			/* copy backwards */
			src += length;
			dst += length;
			do
				*--dst = *--src;
			while (--length);
		}
	}
}
