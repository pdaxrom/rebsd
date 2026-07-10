/* Exercise SSA TEMP copy propagation and trivial phi elimination. */
static int
choose_int(int flag, int value)
{
	int a, b, c;

	a = value;
	if (flag)
		b = a;
	else
		b = a;
	c = b;
	return c;
}

static double
choose_double(int flag, double value)
{
	double a, b, c;

	a = value;
	if (flag)
		b = a;
	else
		b = a;
	c = b;
	return c;
}

int
main(void)
{
	if (choose_int(0, 37) != 37 || choose_int(1, -91) != -91)
		return 1;
	if (choose_double(0, 3.25) != 3.25 ||
	    choose_double(1, -7.5) != -7.5)
		return 2;
	return 0;
}
