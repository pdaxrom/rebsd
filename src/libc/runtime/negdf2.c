//===-- lib/negdf2.c - Double-precision negation -----------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI double
__negdf2(double a)
{
	return fromRep(toRep(a) ^ signBit);
}
