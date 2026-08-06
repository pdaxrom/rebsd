#include <math.h>

static int
check_float(void)
{
        float zero, negzero, one, inf, qnan;

        zero = 0.0f;
        negzero = -0.0f;
        one = 1.0f;
        inf = INFINITY;
        qnan = (nanf)("");

        if (fpclassify(zero) != FP_ZERO)
                return 1;
        if (!isfinite(one) || !isnormal(one))
                return 2;
        if (isfinite(inf) || !isinf(inf) || fpclassify(inf) != FP_INFINITE)
                return 3;
        if (!isnan(qnan) || fpclassify(qnan) != FP_NAN)
                return 4;
        if (!signbit(negzero) || signbit(zero))
                return 5;
        if (!(isnan)((double)qnan))
                return 6;
        if (!(isinf)((double)inf))
                return 7;
        return 0;
}

static int
check_double(void)
{
        double zero, negzero, one, inf, qnan;

        zero = 0.0;
        negzero = -0.0;
        one = 1.0;
        inf = INFINITY;
        qnan = (nan)("");

        if (fpclassify(zero) != FP_ZERO)
                return 10;
        if (!isfinite(one) || !isnormal(one))
                return 11;
        if (isfinite(inf) || !isinf(inf) || fpclassify(inf) != FP_INFINITE)
                return 12;
        if (!isnan(qnan) || fpclassify(qnan) != FP_NAN)
                return 13;
        if (!signbit(negzero) || signbit(zero))
                return 14;
        if (!(isnan)(qnan))
                return 15;
        if (!(isinf)(inf))
                return 16;
        return 0;
}

static int
check_long_double(void)
{
        long double zero, negzero, one, inf, qnan;

        zero = 0.0L;
        negzero = -0.0L;
        one = 1.0L;
        inf = (long double)INFINITY;
        qnan = (nanl)("");

        if (fpclassify(zero) != FP_ZERO)
                return 20;
        if (!isfinite(one) || !isnormal(one))
                return 21;
        if (isfinite(inf) || !isinf(inf) || fpclassify(inf) != FP_INFINITE)
                return 22;
        if (!isnan(qnan) || fpclassify(qnan) != FP_NAN)
                return 23;
        if (!signbit(negzero) || signbit(zero))
                return 24;
        return 0;
}

int
main(void)
{
        int rc;

        rc = check_float();
        if (rc)
                return rc;
        rc = check_double();
        if (rc)
                return rc;
        return check_long_double();
}
