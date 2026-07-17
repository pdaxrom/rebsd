#include <math.h>

#if defined(__mips_hard_float)
union float_bits {
	float value;
	unsigned bits;
};

union double_bits {
	double value;
	unsigned long long bits;
};

static volatile unsigned float_source;
static volatile unsigned long long double_source;

static float
builtin_float_abs(float value)
{
	return __builtin_fabsf(value);
}

static double
plain_double_abs(double value)
{
	return fabs(value);
}

static double
builtin_double_abs(double value)
{
	return __builtin_fabs(value);
}

static long double
builtin_long_double_abs(long double value)
{
	return __builtin_fabsl(value);
}

static int
check_float(unsigned input, unsigned expected)
{
	union float_bits value;

	float_source = input;
	value.bits = float_source;
	value.value = builtin_float_abs(value.value);
	return value.bits != expected;
}

static int
check_double(double (*function)(double), unsigned long long input,
    unsigned long long expected)
{
	union double_bits value;

	double_source = input;
	value.bits = double_source;
	value.value = function(value.value);
	return value.bits != expected;
}

static int
check_long_double(unsigned long long input, unsigned long long expected)
{
	union double_bits value;
	long double result;

	if (sizeof(result) != sizeof(value.value))
		return 0;
	double_source = input;
	value.bits = double_source;
	result = builtin_long_double_abs((long double)value.value);
	value.value = (double)result;
	return value.bits != expected;
}
#endif

int
main(void)
{
#if defined(__mips_hard_float)
	if (check_float(0x80000000U, 0) ||
	    check_float(0xc0600000U, 0x40600000U) ||
	    check_float(0xff800000U, 0x7f800000U) ||
	    check_float(0xff800001U, 0x7f800001U))
		return 1;
	if (check_double(plain_double_abs, 0x8000000000000000ULL, 0) ||
	    check_double(plain_double_abs, 0xc00c000000000000ULL,
	    0x400c000000000000ULL) ||
	    check_double(plain_double_abs, 0xfff0000000000000ULL,
	    0x7ff0000000000000ULL) ||
	    check_double(plain_double_abs, 0xfff0000000000001ULL,
	    0x7ff0000000000001ULL))
		return 2;
	if (check_double(builtin_double_abs, 0x8000000000000000ULL, 0) ||
	    check_double(builtin_double_abs, 0xfff0000000000001ULL,
	    0x7ff0000000000001ULL))
		return 3;
	if (check_long_double(0x8000000000000000ULL, 0) ||
	    check_long_double(0xfff0000000000001ULL,
	    0x7ff0000000000001ULL))
		return 4;
#endif
	return 0;
}
