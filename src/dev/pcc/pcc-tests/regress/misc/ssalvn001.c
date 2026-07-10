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

int
main(void)
{
	if (repeated_expression(19, 4) != 96)
		return 1;
	if (memory_barrier(7, 8) != -32)
		return 2;
	return barrier != 1;
}
