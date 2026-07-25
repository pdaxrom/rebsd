/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <stdio.h>
#include <stdarg.h>

int
vsnprintf(char *str, size_t nbytes, const char *fmt, va_list args)
{
    FILE _strbuf;
    int result;

    _strbuf._flag = _IOWRT+_IOSTRG;
    _strbuf._ptr = str;
    _strbuf._cnt = nbytes == 0 ? 0 :
        (nbytes - 1 > 0x7fffffffU ? 0x7fffffff : (int)nbytes - 1);
    result = _doprnt(fmt, args, &_strbuf);
    if (nbytes != 0)
        *_strbuf._ptr = '\0';
    return result;
}

int
snprintf (char *str, size_t nbytes, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start (args, fmt);
	n = vsnprintf(str, nbytes, fmt, args);
	va_end (args);
	return n;
}
