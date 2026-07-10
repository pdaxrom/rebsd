/* Exercise true and false branches made constant by SSA propagation. */
static int
constant_true(int selector)
{
	int flag;

	if (selector)
		flag = 1;
	else
		flag = 1;
	if (flag)
		return 17;
	return 29;
}

static int
constant_false(int selector)
{
	int flag;

	if (selector)
		flag = 0;
	else
		flag = 0;
	if (flag)
		return 31;
	return 43;
}

int
main(void)
{
	if (constant_true(0) != 17 || constant_true(1) != 17)
		return 1;
	if (constant_false(0) != 43 || constant_false(1) != 43)
		return 2;
	return 0;
}
