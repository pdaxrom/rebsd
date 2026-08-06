#include <stdio.h>

static long long
divide(long long value, int divisor)
{
	return value / divisor;
}

int
main(void)
{
	long long value = divide(864197523LL, 9);
	double fp = 1.5 * 4.0;

	if (sizeof(long double) != 8 || value != 96021947LL || fp != 6.0)
		return 1;
	printf("i686 pcc smoke ok\n");
	return 0;
}
