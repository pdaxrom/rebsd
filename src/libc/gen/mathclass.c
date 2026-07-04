/*
 * ReBSD C99 floating-point classification helpers.
 *
 * ReBSD/MIPS currently uses IEEE-754 binary32 floats and IEEE-754 binary64
 * doubles.  Long double follows the active target ABI and is classified via
 * the matching ReBSD representation.
 */
#define _MATH_IMPL
#include <math.h>

int
__fpclassifyf(float x)
{
        union {
                unsigned int u32;
                float f32;
        } u;
        unsigned int abs;

        u.f32 = x;
        abs = u.u32 & 0x7fffffffU;
        if (abs > 0x7f800000U)
                return FP_NAN;
        if (abs == 0x7f800000U)
                return FP_INFINITE;
        if (abs == 0)
                return FP_ZERO;
        if (abs < 0x00800000U)
                return FP_SUBNORMAL;
        return FP_NORMAL;
}

int
__fpclassifyd(double x)
{
#ifdef TARGET_DOUBLE_IS_FLOAT
        return __fpclassifyf((float)x);
#else
        union {
                unsigned long long u64;
                double f64;
        } u;
        unsigned long long abs;

        u.f64 = x;
        abs = u.u64 & 0x7fffffffffffffffULL;
        if (abs > 0x7ff0000000000000ULL)
                return FP_NAN;
        if (abs == 0x7ff0000000000000ULL)
                return FP_INFINITE;
        if (abs == 0)
                return FP_ZERO;
        if (abs < 0x0010000000000000ULL)
                return FP_SUBNORMAL;
        return FP_NORMAL;
#endif
}

int
__fpclassifyl(long double x)
{
        if (sizeof(long double) == sizeof(float))
                return __fpclassifyf((float)x);
        return __fpclassifyd((double)x);
}

int
__rebsd_signbitf(float x)
{
        union {
                unsigned int u32;
                float f32;
        } u;

        u.f32 = x;
        return (u.u32 >> 31) & 1;
}

int
__rebsd_signbitd(double x)
{
#ifdef TARGET_DOUBLE_IS_FLOAT
        return __rebsd_signbitf((float)x);
#else
        union {
                unsigned long long u64;
                double f64;
        } u;

        u.f64 = x;
        return (int)(u.u64 >> 63);
#endif
}

int
__rebsd_signbitl(long double x)
{
        if (sizeof(long double) == sizeof(float))
                return __rebsd_signbitf((float)x);
        return __rebsd_signbitd((double)x);
}

int
__isfinitef(float x)
{
        int c;

        c = __fpclassifyf(x);
        return c != FP_NAN && c != FP_INFINITE;
}

int
__isfinited(double x)
{
        int c;

        c = __fpclassifyd(x);
        return c != FP_NAN && c != FP_INFINITE;
}

int
__isfinitel(long double x)
{
        int c;

        c = __fpclassifyl(x);
        return c != FP_NAN && c != FP_INFINITE;
}

int
__isnormalf(float x)
{
        return __fpclassifyf(x) == FP_NORMAL;
}

int
__isnormald(double x)
{
        return __fpclassifyd(x) == FP_NORMAL;
}

int
__isnormall(long double x)
{
        return __fpclassifyl(x) == FP_NORMAL;
}

int
__rebsd_isinff(float x)
{
        if (__fpclassifyf(x) != FP_INFINITE)
                return 0;
        return __rebsd_signbitf(x) ? -1 : 1;
}

int
__rebsd_isinfd(double x)
{
        if (__fpclassifyd(x) != FP_INFINITE)
                return 0;
        return __rebsd_signbitd(x) ? -1 : 1;
}

int
__rebsd_isinfl(long double x)
{
        if (__fpclassifyl(x) != FP_INFINITE)
                return 0;
        return __rebsd_signbitl(x) ? -1 : 1;
}
