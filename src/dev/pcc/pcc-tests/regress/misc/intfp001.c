/* Integer-to-floating conversions must preserve unsigned and 64-bit values. */

volatile unsigned int ui = 0xd0cf4b50U;
volatile unsigned long ul = 0xd0cf4b50UL;
volatile long long smin = (-0x7fffffffffffffffLL - 1);
volatile long long smax = 0x7fffffffffffffffLL;
volatile unsigned long long umax = 0xffffffffffffffffULL;

int
main(void)
{
	if ((double)ui != 3503246160.0)
		return 1;
	if ((long double)ul != 3503246160.0)
		return 2;
	if ((double)smin != -9223372036854775808.0)
		return 3;
	if ((double)smax != 9223372036854775808.0)
		return 4;
	if ((double)umax != 18446744073709551616.0)
		return 5;
	return 0;
}
