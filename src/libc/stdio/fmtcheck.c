/*	$NetBSD: fmtcheck.c,v 1.8 2008/04/28 20:22:59 martin Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code was contributed to The NetBSD Foundation by Allen Briggs.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum fmtcheck_type {
	FMT_START,
	FMT_INT,
	FMT_LONG,
	FMT_QUAD,
	FMT_SHORT_POINTER,
	FMT_INT_POINTER,
	FMT_LONG_POINTER,
	FMT_QUAD_POINTER,
	FMT_DOUBLE,
	FMT_LONG_DOUBLE,
	FMT_STRING,
	FMT_WIDTH,
	FMT_PRECISION,
	FMT_DONE,
	FMT_UNKNOWN
};

#define RETURN(pf, f, r) do { *(pf) = (f); return (r); } while (0)

static enum fmtcheck_type
next_after_precision(const char **pf)
{
	const char *f = *pf;
	int sh = 0, lg = 0, quad = 0, longdouble = 0;

	switch (*f) {
	case 'h':
		f++;
		if (*f == 'h')
			f++;
		sh = 1;
		break;
	case 'l':
		f++;
		if (*f == 'l') {
			f++;
			quad = 1;
		} else {
			lg = 1;
		}
		break;
	case 'q':
	case 'j':
		f++;
		quad = 1;
		break;
	case 'z':
	case 't':
		/*
		 * size_t and ptrdiff_t have integer rank on this ABI.  Keeping
		 * them in the integer class also accepts the equivalent native
		 * conversion used by older format strings.
		 */
		f++;
		break;
	case 'L':
		f++;
		longdouble = 1;
		break;
	default:
		break;
	}
	if (!*f)
		RETURN(pf, f, FMT_UNKNOWN);

	if (strchr("diouxX", *f) != NULL) {
		if (longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		if (lg)
			RETURN(pf, f, FMT_LONG);
		if (quad)
			RETURN(pf, f, FMT_QUAD);
		/*
		 * char and short arguments are promoted to int in a variadic
		 * call, so %hhd, %hd, and %d consume the same argument type.
		 */
		RETURN(pf, f, FMT_INT);
	}
	if (*f == 'n') {
		if (longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		if (sh)
			RETURN(pf, f, FMT_SHORT_POINTER);
		if (lg)
			RETURN(pf, f, FMT_LONG_POINTER);
		if (quad)
			RETURN(pf, f, FMT_QUAD_POINTER);
		RETURN(pf, f, FMT_INT_POINTER);
	}
	if (strchr("DOU", *f) != NULL) {
		if (sh || lg || quad || longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		RETURN(pf, f, FMT_LONG);
	}
	if (strchr("aAeEfFgG", *f) != NULL) {
		if (longdouble)
			RETURN(pf, f, FMT_LONG_DOUBLE);
		if (sh || lg || quad)
			RETURN(pf, f, FMT_UNKNOWN);
		RETURN(pf, f, FMT_DOUBLE);
	}
	if (*f == 'c') {
		if (sh || lg || quad || longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		RETURN(pf, f, FMT_INT);
	}
	if (*f == 's') {
		if (sh || lg || quad || longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		RETURN(pf, f, FMT_STRING);
	}
	if (*f == 'p') {
		if (sh || lg || quad || longdouble)
			RETURN(pf, f, FMT_UNKNOWN);
		RETURN(pf, f, FMT_LONG);
	}
	RETURN(pf, f, FMT_UNKNOWN);
}

static enum fmtcheck_type
next_after_width(const char **pf)
{
	const char *f = *pf;

	if (*f == '.') {
		f++;
		if (*f == '*')
			RETURN(pf, f, FMT_PRECISION);
		while (isdigit((unsigned char)*f))
			f++;
		if (!*f)
			RETURN(pf, f, FMT_UNKNOWN);
	}
	*pf = f;
	return next_after_precision(pf);
}

static enum fmtcheck_type
next_format(const char **pf, enum fmtcheck_type previous)
{
	const char *f;

	if (previous == FMT_WIDTH) {
		(*pf)++;
		return next_after_width(pf);
	}
	if (previous == FMT_PRECISION) {
		(*pf)++;
		return next_after_precision(pf);
	}

	f = *pf;
	for (;;) {
		f = strchr(f, '%');
		if (f == NULL)
			RETURN(pf, f, FMT_DONE);
		f++;
		if (!*f)
			RETURN(pf, f, FMT_UNKNOWN);
		if (*f != '%')
			break;
		f++;
	}

	while (*f && strchr("#0- +'", *f) != NULL)
		f++;
	if (*f == '*')
		RETURN(pf, f, FMT_WIDTH);
	while (isdigit((unsigned char)*f))
		f++;
	if (!*f)
		RETURN(pf, f, FMT_UNKNOWN);

	*pf = f;
	return next_after_width(pf);
}

const char *
fmtcheck(const char *format, const char *fallback)
{
	const char *fp, *fallbackp;
	enum fmtcheck_type ft, fallbackt;

	if (format == NULL)
		return fallback;

	fp = format;
	fallbackp = fallback;
	ft = FMT_START;
	fallbackt = FMT_START;
	while ((ft = next_format(&fp, ft)) != FMT_DONE) {
		if (ft == FMT_UNKNOWN)
			return fallback;
		fallbackt = next_format(&fallbackp, fallbackt);
		if (ft != fallbackt)
			return fallback;
	}
	if (next_format(&fallbackp, fallbackt) != FMT_DONE)
		return fallback;
	return format;
}
