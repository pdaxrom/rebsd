/* Exercise equal integer constants reaching an SSA phi. */
static int
choose_constant(int flag)
{
	int value;

	if (flag)
		value = 73;
	else
		value = 73;
	return value;
}

static unsigned
loop_constant(unsigned count)
{
	unsigned value;

	value = 0x13579bdfU;
	while (count-- != 0)
		value = value;
	return value;
}

int
main(void)
{
	if (choose_constant(0) != 73 || choose_constant(1) != 73)
		return 1;
	if (loop_constant(17) != 0x13579bdfU)
		return 2;
	return 0;
}
