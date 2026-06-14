//===-- floatdidf.c - int64 -> double-precision conversion ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t
__floatdidf(di_int a)
{
    const int aWidth = sizeof a * CHAR_BIT;
    du_int abs;
    rep_t sign;
    int exponent;
    rep_t result;

    if (a == 0)
        return fromRep(0);

    sign = 0;
    abs = (du_int)a;
    if (a < 0) {
        sign = signBit;
        abs = -abs;
    }

    exponent = (aWidth - 1) - __clzdi2((di_int)abs);
    if (exponent <= significandBits) {
        int shift = significandBits - exponent;

        result = (rep_t)abs << shift;
        result ^= implicitBit;
    } else {
        int shift = exponent - significandBits;
        rep_t round;

        result = (rep_t)(abs >> shift);
        result ^= implicitBit;
        round = (rep_t)(abs << (typeWidth - shift));
        if (round > signBit)
            result++;
        if (round == signBit)
            result += result & 1;
    }

    result += (rep_t)(exponent + exponentBias) << significandBits;
    return fromRep(result | sign);
}
