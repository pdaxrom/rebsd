//===-- divmoddi4.c - Implement __divmoddi4 -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

// Effects: if rem != 0, *rem = a % b
// Returns: a / b

COMPILER_RT_ABI di_int __divmoddi4(di_int a, di_int b, di_int *rem) {
  const unsigned n_dword_bits = sizeof(di_int) * CHAR_BIT;
  const di_int a_sign = a >> (n_dword_bits - 1);
  const di_int b_sign = b >> (n_dword_bits - 1);
  const du_int a_abs = (du_int)((a ^ a_sign) - a_sign);
  const du_int b_abs = (du_int)((b ^ b_sign) - b_sign);
  du_int unsigned_rem;
  di_int quotient;

  quotient = (di_int)__udivmoddi4(a_abs, b_abs,
      rem != 0 ? &unsigned_rem : (du_int *)0);
  quotient = (quotient ^ (a_sign ^ b_sign)) - (a_sign ^ b_sign);
  if (rem != 0)
    *rem = ((di_int)unsigned_rem ^ a_sign) - a_sign;
  return quotient;
}
