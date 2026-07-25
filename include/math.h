/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef _MATH_H_
#define _MATH_H_

double  fabs(double), floor(double), ceil(double);
double  sqrt(double), hypot(double, double);
double  sin(double), cos(double), tan(double);
double  asin(double), acos(double), atan(double), atan2(double, double);
double  exp(double), log(double), log10(double), pow(double, double);
double  sinh(double), cosh(double), tanh(double);
double  j0(double), j1(double), jn(int, double);
double  y0(double), y1(double), yn(int, double);

#define FP_NAN          0
#define FP_INFINITE     1
#define FP_ZERO         2
#define FP_SUBNORMAL    3
#define FP_NORMAL       4

int __fpclassifyf(float);
int __fpclassifyd(double);
int __fpclassifyl(long double);
int __isfinitef(float);
int __isfinited(double);
int __isfinitel(long double);
int __isnormalf(float);
int __isnormald(double);
int __isnormall(long double);
int __rebsd_isinff(float);
int __rebsd_isinfd(double);
int __rebsd_isinfl(long double);
int __rebsd_signbitf(float);
int __rebsd_signbitd(double);
int __rebsd_signbitl(long double);
int __rebsd_isgreaterl(long double, long double);
int __rebsd_isgreaterequall(long double, long double);
int __rebsd_islessl(long double, long double);
int __rebsd_islessequall(long double, long double);
int __rebsd_islessgreaterl(long double, long double);
int __rebsd_isunorderedl(long double, long double);

double nan(const char *);
float nanf(const char *);
long double nanl(const char *);

#if defined(__GNUC__) || defined(__PCC__)
#define HUGE_VAL    __builtin_huge_val()
#define HUGE_VALF   __builtin_huge_valf()
#define HUGE_VALL   __builtin_huge_vall()
#define INFINITY    __builtin_inff()
#define NAN         __builtin_nanf("")
#define nan(x)      __builtin_nan(x)
#define nanf(x)     __builtin_nanf(x)
#define nanl(x)     __builtin_nanl(x)
#else
#define HUGE_VAL    1.7976931348623157e+308
#define HUGE_VALF   3.40282347e+38F
#define HUGE_VALL   HUGE_VAL
#define INFINITY    HUGE_VALF
#define NAN         nanf("")
#endif

#define signbit(x) \
    (sizeof(x) == sizeof(float) ? __rebsd_signbitf((float)(x)) : \
    ((sizeof(x) == sizeof(long double) && sizeof(long double) != sizeof(double)) ? \
    __rebsd_signbitl((long double)(x)) : __rebsd_signbitd((double)(x))))

int isnanf(float x);
int isnan(double x);

int isinff(float x);
int isinf(double x);

float modff(float x, float *iptr);
double modf(double x, double *iptr);

float frexpf(float x, int *exp);
double frexp(double x, int *exp);

float ldexpf(float x, int exp);
double ldexp(double x, int exp);

double fmod(double x, double y);

#ifndef _MATH_IMPL
#define fpclassify(x) \
    (sizeof(x) == sizeof(float) ? __fpclassifyf((float)(x)) : \
    ((sizeof(x) == sizeof(long double) && sizeof(long double) != sizeof(double)) ? \
    __fpclassifyl((long double)(x)) : __fpclassifyd((double)(x))))
#define isfinite(x) \
    (sizeof(x) == sizeof(float) ? __isfinitef((float)(x)) : \
    ((sizeof(x) == sizeof(long double) && sizeof(long double) != sizeof(double)) ? \
    __isfinitel((long double)(x)) : __isfinited((double)(x))))
#define isnormal(x) \
    (sizeof(x) == sizeof(float) ? __isnormalf((float)(x)) : \
    ((sizeof(x) == sizeof(long double) && sizeof(long double) != sizeof(double)) ? \
    __isnormall((long double)(x)) : __isnormald((double)(x))))
#define isinf(x) \
    (sizeof(x) == sizeof(float) ? __rebsd_isinff((float)(x)) : \
    ((sizeof(x) == sizeof(long double) && sizeof(long double) != sizeof(double)) ? \
    __rebsd_isinfl((long double)(x)) : __rebsd_isinfd((double)(x))))
#define isnan(x) (fpclassify(x) == FP_NAN)
#define isgreater(x, y) \
    __rebsd_isgreaterl((long double)(x), (long double)(y))
#define isgreaterequal(x, y) \
    __rebsd_isgreaterequall((long double)(x), (long double)(y))
#define isless(x, y) \
    __rebsd_islessl((long double)(x), (long double)(y))
#define islessequal(x, y) \
    __rebsd_islessequall((long double)(x), (long double)(y))
#define islessgreater(x, y) \
    __rebsd_islessgreaterl((long double)(x), (long double)(y))
#define isunordered(x, y) \
    __rebsd_isunorderedl((long double)(x), (long double)(y))
#endif

#if !defined(_ANSI_SOURCE) && !defined(_POSIX_SOURCE)

#define M_E             2.7182818284590452354   /* e */
#define M_LOG2E         1.4426950408889634074   /* log 2e */
#define M_LOG10E        0.43429448190325182765  /* log 10e */
#define M_LN2           0.69314718055994530942  /* log e2 */
#define M_LN10          2.30258509299404568402  /* log e10 */
#define M_PI            3.14159265358979323846  /* pi */
#define M_PI_2          1.57079632679489661923  /* pi/2 */
#define M_PI_4          0.78539816339744830962  /* pi/4 */
#define M_1_PI          0.31830988618379067154  /* 1/pi */
#define M_2_PI          0.63661977236758134308  /* 2/pi */
#define M_2_SQRTPI      1.12837916709551257390  /* 2/sqrt(pi) */
#define M_SQRT2         1.41421356237309504880  /* sqrt(2) */
#define M_SQRT1_2       0.70710678118654752440  /* 1/sqrt(2) */

#endif /* !_ANSI_SOURCE && !_POSIX_SOURCE */

#endif /* _MATH_H_ */
