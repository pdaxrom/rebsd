/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include <stdlib.h>

int
abs(int arg)
{
	return(arg < 0 ? -arg : arg);
}
