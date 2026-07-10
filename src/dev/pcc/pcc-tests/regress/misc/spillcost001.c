static unsigned
hold(unsigned value)
{
	return value * 17U + 3U;
}

#define STEP(v, prev, salt) \
	(v) = ((v) * 33U + (prev) + i + j + (salt)) ^ ((v) >> 3)

static unsigned
scalar_pressure(unsigned n)
{
	unsigned v0 = 1, v1 = 2, v2 = 3, v3 = 4, v4 = 5, v5 = 6;
	unsigned v6 = 7, v7 = 8, v8 = 9, v9 = 10, v10 = 11, v11 = 12;
	unsigned v12 = 13, v13 = 14, v14 = 15, v15 = 16, v16 = 17, v17 = 18;
	unsigned i, j;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n + 1; j++) {
			STEP(v0, v17, 1); STEP(v1, v0, 2);
			STEP(v2, v1, 3); STEP(v3, v2, 4);
			STEP(v4, v3, 5); STEP(v5, v4, 6);
			STEP(v6, v5, 7); STEP(v7, v6, 8);
			STEP(v8, v7, 9); STEP(v9, v8, 10);
			STEP(v10, v9, 11); STEP(v11, v10, 12);
			STEP(v12, v11, 13); STEP(v13, v12, 14);
			STEP(v14, v13, 15); STEP(v15, v14, 16);
			STEP(v16, v15, 17); STEP(v17, v16, 18);
		}
		v0 += hold(v8 + v9 + i);
	}
	return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 +
	    v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17;
}

static unsigned
array_reference(unsigned n)
{
	unsigned v[18];
	unsigned i, j, k, prev;

	for (k = 0; k < 18; k++)
		v[k] = k + 1;
	for (i = 0; i < n; i++) {
		for (j = 0; j < n + 1; j++) {
			for (k = 0; k < 18; k++) {
				prev = k == 0 ? v[17] : v[k - 1];
				v[k] = (v[k] * 33U + prev + i + j + k + 1U) ^
				    (v[k] >> 3);
			}
		}
		v[0] += hold(v[8] + v[9] + i);
	}
	prev = 0;
	for (k = 0; k < 18; k++)
		prev += v[k];
	return prev;
}

int
main(void)
{
	return scalar_pressure(5) == array_reference(5) ? 0 : 1;
}
