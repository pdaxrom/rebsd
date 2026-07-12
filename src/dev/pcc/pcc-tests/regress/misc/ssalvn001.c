static volatile int barrier;

static int
repeated_expression(int left, int right)
{
	int difference, first, second, sum;

	sum = left + right;
	difference = left - right;
	first = sum ^ difference;
	second = sum ^ difference;
	return first * 3 + second;
}

static int
memory_barrier(int left, int right)
{
	int difference, first, second, sum;

	sum = left + right;
	difference = left - right;
	first = sum ^ difference;
	barrier++;
	second = sum ^ difference;
	return first + second;
}

static int
repeated_scale(int *values, int scale, int index)
{
	int first, second;

	first = values[scale * index];
	second = values[scale * index + 1];
	return first + second;
}

static int
changed_scale(int *values, int scale, int index)
{
	int first, second;

	first = values[scale * index];
	index++;
	second = values[scale * index];
	return first + second;
}

static int
repeated_shift(int *left, int *right, int index)
{
	int first, second;

	first = left[index];
	second = right[index];
	return first + second;
}

static int
changed_shift(int *left, int *right, int index)
{
	int first, second;

	first = left[index];
	index++;
	second = right[index];
	return first + second;
}

int
main(void)
{
	int values[16];
	int i;

	for (i = 0; i < 16; i++)
		values[i] = i * 3;
	if (repeated_expression(19, 4) != 96)
		return 1;
	if (memory_barrier(7, 8) != -32)
		return 2;
	if (repeated_scale(values, 3, 2) != 39)
		return 3;
	if (changed_scale(values, 3, 2) != 45)
		return 4;
	if (repeated_shift(values, values + 1, 3) != 21)
		return 5;
	if (changed_shift(values, values + 1, 3) != 24)
		return 6;
	return barrier != 1;
}
