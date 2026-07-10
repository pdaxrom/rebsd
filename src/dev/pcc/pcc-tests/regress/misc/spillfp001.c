static double
holdfp(double value)
{
	return value * 0.5 + 1.0;
}

static double
scalar_pressure(int n)
{
	double v0 = 1.0, v1 = 2.0, v2 = 3.0, v3 = 4.0, v4 = 5.0;
	double v5 = 6.0, v6 = 7.0, v7 = 8.0, v8 = 9.0, v9 = 10.0;
	int i, j;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n + 1; j++) {
			v0 += v9 * 0.25 + i + j;
			v1 += v0 * 0.25 + i + j;
			v2 += v1 * 0.25 + i + j;
			v3 += v2 * 0.25 + i + j;
			v4 += v3 * 0.25 + i + j;
			v5 += v4 * 0.25 + i + j;
			v6 += v5 * 0.25 + i + j;
			v7 += v6 * 0.25 + i + j;
			v8 += v7 * 0.25 + i + j;
			v9 += v8 * 0.25 + i + j;
		}
		v0 += holdfp(v5);
	}
	return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9;
}

static double
array_reference(int n)
{
	double v[10], sum;
	int i, j, k;

	for (k = 0; k < 10; k++)
		v[k] = k + 1;
	for (i = 0; i < n; i++) {
		for (j = 0; j < n + 1; j++)
			for (k = 0; k < 10; k++)
				v[k] += v[k == 0 ? 9 : k - 1] * 0.25 + i + j;
		v[0] += holdfp(v[5]);
	}
	sum = 0.0;
	for (k = 0; k < 10; k++)
		sum += v[k];
	return sum;
}

int
main(void)
{
	return scalar_pressure(4) == array_reference(4) ? 0 : 1;
}
