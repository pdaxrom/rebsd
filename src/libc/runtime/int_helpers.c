//===-- int_helpers.c - libgcc-compatible integer helpers -----------------===//
//
// These helpers are emitted by compilers for integer operations on 32-bit
// targets.  When PCC's libpcc is linked, libpcc owns the 64-bit shift helpers
// and libc keeps only the non-overlapping bit-scan helpers here.
//
//===----------------------------------------------------------------------===//

#include "int_lib.h"

#ifndef LIBC_USES_LIBPCC_RUNTIME
COMPILER_RT_ABI di_int
__ashldi3(di_int a, int b)
{
    udwords x, r;
    unsigned shift;

    shift = (unsigned)b;
    x.all = (du_int)a;
    if (shift == 0)
        return a;
    if (shift >= 64)
        return 0;
    if (shift >= 32) {
        r.s.high = x.s.low << (shift - 32);
        r.s.low = 0;
    } else {
        r.s.high = (x.s.high << shift) | (x.s.low >> (32 - shift));
        r.s.low = x.s.low << shift;
    }
    return (di_int)r.all;
}

COMPILER_RT_ABI di_int
__ashrdi3(di_int a, int b)
{
    dwords x, r;
    unsigned shift;

    shift = (unsigned)b;
    x.all = a;
    if (shift == 0)
        return a;
    if (shift >= 64)
        return x.s.high < 0 ? (di_int)-1 : (di_int)0;
    if (shift >= 32) {
        r.s.high = x.s.high >> 31;
        r.s.low = (su_int)(x.s.high >> (shift - 32));
    } else {
        r.s.high = x.s.high >> shift;
        r.s.low = (x.s.low >> shift) | ((su_int)x.s.high << (32 - shift));
    }
    return r.all;
}

COMPILER_RT_ABI di_int
__lshrdi3(di_int a, int b)
{
    udwords x, r;
    unsigned shift;

    shift = (unsigned)b;
    x.all = (du_int)a;
    if (shift == 0)
        return a;
    if (shift >= 64)
        return 0;
    if (shift >= 32) {
        r.s.high = 0;
        r.s.low = x.s.high >> (shift - 32);
    } else {
        r.s.high = x.s.high >> shift;
        r.s.low = (x.s.low >> shift) | (x.s.high << (32 - shift));
    }
    return (di_int)r.all;
}
#endif

COMPILER_RT_ABI int
__clzsi2(si_int a)
{
    su_int x;
    int n;

    x = (su_int)a;
    if (x == 0)
        return 32;
    n = 0;
    if ((x & 0xffff0000u) == 0) {
        n += 16;
        x <<= 16;
    }
    if ((x & 0xff000000u) == 0) {
        n += 8;
        x <<= 8;
    }
    if ((x & 0xf0000000u) == 0) {
        n += 4;
        x <<= 4;
    }
    if ((x & 0xc0000000u) == 0) {
        n += 2;
        x <<= 2;
    }
    if ((x & 0x80000000u) == 0)
        n += 1;
    return n;
}

COMPILER_RT_ABI int
__ctzsi2(si_int a)
{
    su_int x;
    int n;

    x = (su_int)a;
    if (x == 0)
        return 32;
    n = 0;
    if ((x & 0x0000ffffu) == 0) {
        n += 16;
        x >>= 16;
    }
    if ((x & 0x000000ffu) == 0) {
        n += 8;
        x >>= 8;
    }
    if ((x & 0x0000000fu) == 0) {
        n += 4;
        x >>= 4;
    }
    if ((x & 0x00000003u) == 0) {
        n += 2;
        x >>= 2;
    }
    if ((x & 0x00000001u) == 0)
        n += 1;
    return n;
}

COMPILER_RT_ABI int
__ffssi2(si_int a)
{
    if (a == 0)
        return 0;
    return __ctzsi2(a) + 1;
}

COMPILER_RT_ABI int
__clzdi2(di_int a)
{
    udwords x;

    x.all = (du_int)a;
    if (x.s.high != 0)
        return __clzsi2((si_int)x.s.high);
    return 32 + __clzsi2((si_int)x.s.low);
}

COMPILER_RT_ABI int
__ctzdi2(di_int a)
{
    udwords x;

    x.all = (du_int)a;
    if (x.s.low != 0)
        return __ctzsi2((si_int)x.s.low);
    return 32 + __ctzsi2((si_int)x.s.high);
}
