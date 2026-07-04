/*
 * ReBSD C99 NaN constructors.
 *
 * The tag string is accepted for API compatibility; ReBSD currently returns
 * the default quiet NaN for the active IEEE target format.
 */
#define _MATH_IMPL
#include <math.h>

float
nanf(const char *tagp)
{
        union {
                unsigned int u32;
                float f32;
        } u;

        (void)tagp;
        u.u32 = 0x7fc00000U;
        return u.f32;
}

double
nan(const char *tagp)
{
#ifdef TARGET_DOUBLE_IS_FLOAT
        return nanf(tagp);
#else
        union {
                unsigned long long u64;
                double f64;
        } u;

        (void)tagp;
        u.u64 = 0x7ff8000000000000ULL;
        return u.f64;
#endif
}

long double
nanl(const char *tagp)
{
#ifdef TARGET_DOUBLE_IS_FLOAT
        union {
                unsigned int u32;
                float f32;
        } u;

        (void)tagp;
        u.u32 = 0x7fc00000U;
        return (long double)u.f32;
#else
        union {
                unsigned long long u64;
                double f64;
        } u;

        (void)tagp;
        u.u64 = 0x7ff8000000000000ULL;
        return (long double)u.f64;
#endif
}
