static int
counted_up(int first, int limit)
{
	int i, sum;

	sum = 0;
	for (i = first; i < limit; i++)
		sum += i;
	return sum;
}

static unsigned int
counted_unsigned_up(unsigned int first, unsigned int limit)
{
	unsigned int i, sum;

	sum = 0;
	for (i = first; i < limit; i++)
		sum += i;
	return sum;
}

static int
counted_down(int first, int limit)
{
	int i, sum;

	sum = 0;
	for (i = first; i > limit; i--)
		sum += i;
	return sum;
}

static unsigned int
counted_unsigned_down(unsigned int first, unsigned int limit)
{
	unsigned int i, sum;

	sum = 0;
	for (i = first; i > limit; i--)
		sum += i;
	return sum;
}

static int
counted_constant(void)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < 8; i++)
		sum += i;
	return sum;
}

static int
counted_step_two(int first, int limit)
{
	int i, sum;

	sum = 0;
	for (i = first; i < limit; i += 2)
		sum += i;
	return sum;
}

static int
counted_multiblock(int limit)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < limit; i++) {
		if (i & 1)
			sum -= i;
		else
			sum += i;
	}
	return sum;
}

int
main(void)
{
	if (counted_up(2, 7) != 20 || counted_up(7, 7) != 0 ||
	    counted_up(9, 7) != 0)
		return 1;
	if (counted_unsigned_up(2U, 7U) != 20U ||
	    counted_unsigned_up(7U, 7U) != 0U ||
	    counted_unsigned_up(9U, 7U) != 0U)
		return 2;
	if (counted_down(7, 2) != 25 || counted_down(2, 2) != 0 ||
	    counted_down(1, 2) != 0)
		return 3;
	if (counted_unsigned_down(7U, 2U) != 25U ||
	    counted_unsigned_down(2U, 2U) != 0U ||
	    counted_unsigned_down(1U, 2U) != 0U)
		return 4;
	if (counted_constant() != 28)
		return 5;
	if (counted_step_two(1, 8) != 16)
		return 6;
	if (counted_multiblock(8) != -4)
		return 7;
	return 0;
}
