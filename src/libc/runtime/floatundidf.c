//===-- floatundidf.c - uint64 -> double-precision conversion -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t
__floatundidf(du_int a)
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
        rep_t round;

        result = (rep_t)(a >> shift);
        result ^= implicitBit;
        round = (rep_t)(a << (typeWidth - shift));
        if (round > signBit)
            result++;
        if (round == signBit)
            result += result & 1;
    }

    result += (rep_t)(exponent + exponentBias) << significandBits;
    return fromRep(result);
}
