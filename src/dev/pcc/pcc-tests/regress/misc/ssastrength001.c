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

static int
pointer_step_four(const int *values, int count)
{
	int i, sum;

	sum = 0;
	for (i = 1; i + 3 < count; i += 4)
		sum += values[i] + values[i + 1] + values[i + 2] + values[i + 3];
	return sum;
}

static int
pointer_descending_four(const int *values, int count)
{
	int i, sum;

	sum = 0;
	for (i = count - 4; i >= 0; i -= 4)
		sum += values[i] + values[i + 1] + values[i + 2] + values[i + 3];
	return sum;
}

static double
pointer_single_fp(const double *values, int count)
{
	double sum;
	int i;

	sum = 0.0;
	for (i = 0; i < count; i++)
		sum += values[i];
	return sum;
}

static int
pointer_single_int(const int *values, int count)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += values[i];
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
	if (pointer_step_four(values, 16) != 168)
		return 4;
	if (pointer_descending_four(values, 16) != 256)
		return 5;
	if (pointer_single_fp((const double[]){ 1.0, 2.0, 3.0, 4.0 }, 4) != 10.0)
		return 6;
	if (pointer_single_int(values, 8) != 64)
		return 7;
	return 0;
}
