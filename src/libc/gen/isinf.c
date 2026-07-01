/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Changed to return -1 for -Inf by Ulrich Drepper <drepper@cygnus.com>.
 * Public domain.
 */
#include <math.h>

/*
 * isinf(x) returns 1 is x is inf, -1 if x is -inf, else 0;
 * no branching!
 */
int isinf (double x)
{
	union {
		unsigned long long u64;
		double f64;
	} u;
	unsigned long long abs;

	u.f64 = x;
	abs = u.u64 & 0x7fffffffffffffffULL;
	if (abs != 0x7ff0000000000000ULL)
		return 0;
	return (u.u64 >> 63) ? -1 : 1;
}
