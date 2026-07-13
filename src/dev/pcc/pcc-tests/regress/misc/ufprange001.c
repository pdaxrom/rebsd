/* Bounded unsigned expressions may use signed floating-point conversion. */

static volatile unsigned int input = 0xffffffffU;
static volatile unsigned long long_input = 0xffffffffUL;

int
main(void)
{
	unsigned int value;
	unsigned long long_value;

	value = input;
	long_value = long_input;
	if ((double)(value & 0x7fffffffU) != 2147483647.0)
		return 1;
	if ((float)(value >> 1) != 2147483648.0f)
		return 2;
	if ((double)(long_value & 0x003fffffUL) != 4194303.0)
		return 3;
	if ((double)value != 4294967295.0)
		return 4;
	if ((float)value != 4294967296.0f)
		return 5;
	return 0;
}
