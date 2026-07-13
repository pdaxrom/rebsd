double
fp_accumulator(int count, const double *left, const double *right)
{
	double sum = 0.0;
	int i;

	for (i = 0; i < count; i++)
		sum += left[i] * right[i];
	return sum;
}

void
fp_add_half(double *value)
{
	*value += 0.5;
}

static double
fp_address(double seed)
{
	double value = seed;

	fp_add_half(&value);
	return value;
}

static double
fp_helper_call(double value, unsigned long long integer)
{
	return value - (double)integer;
}

int
main(void)
{
	static const double left[] = { 1.0, 2.0, 3.0, 4.0 };
	static const double right[] = { 5.0, 6.0, 7.0, 8.0 };

	if (fp_accumulator(4, left, right) != 70.0)
		return 1;
	if (fp_address(2.0) != 2.5)
		return 2;
	return fp_helper_call(28.0, 1) == 27.0 ? 0 : 3;
}
