static volatile int side_effect;

static int
constant_true(int selector)
{
	int flag;

	if (selector)
		flag = 1;
	else
		flag = 1;
	if (flag)
		return 61;
	side_effect++;
	return 97;
}

static int
constant_false(int selector)
{
	int flag;

	if (selector)
		flag = 0;
	else
		flag = 0;
	if (flag) {
		side_effect++;
		return 101;
	}
	return 67;
}

int
main(void)
{
	if (constant_true(0) != 61 || constant_true(1) != 61)
		return 1;
	if (constant_false(0) != 67 || constant_false(1) != 67)
		return 2;
	return side_effect != 0;
}
