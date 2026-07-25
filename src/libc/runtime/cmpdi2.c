//===-- cmpdi2.c - 64-bit integer comparison helpers ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

/*
 * The GCC helper ABI returns 0, 1, or 2 for less than, equal, or greater
 * than, respectively.
 */
COMPILER_RT_ABI si_int
__cmpdi2(di_int a, di_int b)
{
	dwords left, right;

	left.all = a;
	right.all = b;
	if (left.s.high < right.s.high)
		return 0;
	if (left.s.high > right.s.high)
		return 2;
	if (left.s.low < right.s.low)
		return 0;
	if (left.s.low > right.s.low)
		return 2;
	return 1;
}

COMPILER_RT_ABI si_int
__ucmpdi2(du_int a, du_int b)
{
	udwords left, right;

	left.all = a;
	right.all = b;
	if (left.s.high < right.s.high)
		return 0;
	if (left.s.high > right.s.high)
		return 2;
	if (left.s.low < right.s.low)
		return 0;
	if (left.s.low > right.s.low)
		return 2;
	return 1;
}
