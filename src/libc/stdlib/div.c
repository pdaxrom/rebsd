/*-
 * Copyright (c) 1990, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 */

#include <stdlib.h>

div_t
div(int numer, int denom)
{
        div_t r;

        r.quot = numer / denom;
        r.rem = numer % denom;
        return r;
}
