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

static int
pointer_reverse_pair(const int *values, int count)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += values[31 - i] + values[30 - i];
	return sum;
}

static int
pointer_reverse_pair_dynamic(const int *values, int start, int count)
{
	int i, sum;

	sum = 0;
	for (i = start; i < count; i++)
		sum += values[31 - i] + values[30 - i];
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
static double global_double_values[64];

static unsigned int
masked_constant(const unsigned int *values, unsigned int count)
{
	unsigned int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += values[(i * 17U) & 63U] ^ i;
	return sum;
}

static unsigned int
masked_constant_dynamic(const unsigned int *values, unsigned int start,
    unsigned int count)
{
	unsigned int i, sum;

	sum = 0;
	for (i = start; i < count; i++)
		sum += values[(i * 9U) & 63U] ^ i;
	return sum;
}

static unsigned int
masked_constant_partial(const unsigned int *values, unsigned int count)
{
	unsigned int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += values[(i * 17U) & 60U] ^ i;
	return sum;
}

static unsigned int
unmasked_constant(const unsigned int *values, unsigned int count)
{
	unsigned int i, sum;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += values[i] + i * 17U;
	return sum;
}

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

static double
pointer_single_reverse_global_fp(int count)
{
	double sum;
	int i;

	sum = 0.0;
	for (i = 0; i < count; i++)
		sum += global_double_values[63 - i];
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
		global_double_values[i] = (double)(i + 1) * 0.5;
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
	if (pointer_reverse_pair(values, 16) != 1504)
		return 10;
	if (pointer_single_reverse_global_fp(4) != 125.0)
		return 11;
	if (pointer_reverse_pair_dynamic(values, 4, 16) != 1032)
		return 12;
	if (masked_constant(global_unsigned_values, 64) != 10176U)
		return 13;
	if (masked_constant_dynamic(global_unsigned_values, 5, 45) != 6720U)
		return 14;
	if (masked_constant_partial(global_unsigned_values, 64) != 9312U)
		return 15;
	if (unmasked_constant(global_unsigned_values, 16) != 2688U)
		return 16;
	return 0;
}
