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

static int global_values[64];
static unsigned int global_unsigned_values[64];

static int
pointer_single_global_int(int count)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += global_values[i];
	return sum;
}

static unsigned int
pointer_single_global_unsigned(unsigned int count)
{
	unsigned int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += global_unsigned_values[i];
	return sum;
}

int
main(void)
{
	int values[64];
	int i;

	for (i = 0; i < 64; i++) {
		values[i] = i * 2 + 1;
		global_values[i] = i * 3 + 2;
		global_unsigned_values[i] = (unsigned int)i * 5U + 3U;
	}
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
	if (pointer_single_global_int(8) != 100)
		return 8;
	if (pointer_single_global_unsigned(8) != 164U)
		return 9;
	return 0;
}
