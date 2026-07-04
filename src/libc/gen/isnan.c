/*
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 */
#define _MATH_IMPL
#include <math.h>

/*
 * isnan(x) returns 1 is x is nan, else 0;
 * no branching!
 */
int isnan (double x)
{
	union {
		unsigned long long u64;
		double f64;
	} u;
	unsigned long long abs;

	u.f64 = x;
	abs = u.u64 & 0x7fffffffffffffffULL;
	return abs > 0x7ff0000000000000ULL;
}
