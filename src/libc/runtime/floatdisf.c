//===-- floatdisf.c - int64 -> single-precision conversion ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t __floatundisf(du_int);

COMPILER_RT_ABI fp_t
__floatdisf(di_int a)
{
	if (a < 0)
		return -__floatundisf((du_int)0 - (du_int)a);
	return __floatundisf((du_int)a);
}
