static double left_value = 1.0;
static double right_value = 2.0;

double
leaf(double *value)
{
	return *value;
}

double
combine(double left, double right)
{
	return left * 10.0 + right;
}

int
main(void)
{
	double result = combine(leaf(&left_value), leaf(&right_value));

	return result == 12.0 ? 0 : 1;
}
