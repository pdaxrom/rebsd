/* Floating-to-integer conversions truncate and preserve unsigned values. */

static volatile float float_positive = 3.75f;
static volatile float float_negative = -3.75f;
static volatile double double_positive = 1234567.875;
static volatile double double_negative = -1234567.875;
static volatile double double_unsigned = 3503246160.75;

int
main(void)
{
	if ((int)float_positive != 3)
		return 1;
	if ((int)float_negative != -3)
		return 2;
	if ((int)double_positive != 1234567)
		return 3;
	if ((int)double_negative != -1234567)
		return 4;
	if ((unsigned int)double_unsigned != 3503246160U)
		return 5;
	return 0;
}
