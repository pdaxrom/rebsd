static int
unit_step(const int *values, int count, int stride)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < count; i++) {
		if (i & 1)
			sum += values[i * stride];
		else
			sum -= values[i * stride];
	}
	return sum;
}

static int
descending(const int *values, int count, int stride)
{
	int i, sum;

	sum = 0;
	for (i = count - 1; i >= 0; i--) {
		if (i & 1)
			sum += values[i * stride];
		else
			sum -= values[i * stride];
	}
	return sum;
}

static int
variable_step(const int *values, int count, int stride, int step)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < count; i += step) {
		if (i & 1)
			sum += values[i * stride];
		else
			sum -= values[i * stride];
	}
	return sum;
}

int
main(void)
{
	int values[64];
	int i;

	for (i = 0; i < 64; i++)
		values[i] = i * 2 + 1;
	if (unit_step(values, 8, 3) != 24)
		return 1;
	if (descending(values, 8, 3) != 24)
		return 2;
	if (variable_step(values, 8, 3, 2) != -76)
		return 3;
	return 0;
}
