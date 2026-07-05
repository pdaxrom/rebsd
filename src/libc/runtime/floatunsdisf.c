//===-- lib/floatunsdisf.c - uint64 -> single-precision conversion --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t
__floatunsdisf(du_int a)
{
	const int aWidth = sizeof a * CHAR_BIT;
	int exponent;
	rep_t result;

	if (a == 0)
		return fromRep(0);

	exponent = (aWidth - 1) - __clzdi2((di_int)a);
	if (exponent <= significandBits) {
		int shift = significandBits - exponent;

		result = (rep_t)a << shift;
		result ^= implicitBit;
	} else {
		int shift = exponent - significandBits;
		du_int round;

		result = (rep_t)(a >> shift);
		result ^= implicitBit;
		round = a << (aWidth - shift);
		if (round > ((du_int)1 << (aWidth - 1)))
			result++;
		if (round == ((du_int)1 << (aWidth - 1)))
			result += result & 1;
	}

	result += (rep_t)(exponent + exponentBias) << significandBits;
	return fromRep(result);
}
