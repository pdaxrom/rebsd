/* Exercise a critical loop edge with cyclic integer and FP phi copies. */
static int
run(int n)
{
	int a, b, i, t;
	double x, y, z;

	a = 1;
	b = 2;
	x = 1.25;
	y = -2.5;
	i = 0;
	do {
		t = a;
		a = b;
		b = t;
		z = x;
		x = y;
		y = z;
		i++;
	} while (i < n);

	if (a != 2 || b != 1)
		return 1;
	if (x != -2.5 || y != 1.25)
		return 2;
	return 0;
}

int
main(int argc, char **argv)
{
	(void)argv;
	return run(argc + 16);
}
