/* Exercise bounded static specialization and its generic fallback. */
static int sum_stride(const int *, int, int, int);
static int sum_six(int, int, int, int, int *, int);

static int (*sum_stride_pointer)(const int *, int, int, int) = sum_stride;

int
main(void)
{
	static const int values[] = { 1, 2, 3, 4, 5, 6 };
	int output = 0;
	volatile int stride = 2;

	if (sum_stride(values, 6, 0, 1) != 21)
		return 1;
	if (sum_stride(values, 6, 0, stride) != 9)
		return 2;
	if (sum_stride_pointer(values, 6, 1, 2) != 12)
		return 3;
	if (sum_six(2, 3, 0, 1, &output, 1) != 7 || output != 7)
		return 4;
	return 0;
}

static int
sum_stride(const int *values, int count, int first, int stride)
{
	int result = 0;
	int i;

	for (i = first; i < count; i += stride)
		result += values[i];
	return result;
}

static int
sum_six(int a, int b, int zero, int one, int *out, int tail)
{
	*out = a + b + zero + one + tail;
	return *out;
}
