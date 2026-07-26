/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * bcmp -- vax cmpc3 instruction
 */
int
bcmp(const void *v1, const void *v2, unsigned long length)
{
        register const char *b1 = v1;
        register const char *b2 = v2;
	if (length)
		do
			if (*b1++ != *b2++)
				break;
		while (--length);
	return(length);
}
