static int g0 = 1, g1 = 2, g2 = 3, g3 = 4;
static int g4 = 5, g5 = 6, g6 = 7, g7 = 8;
static int g8 = 9, g9 = 10, g10 = 11, g11 = 12;
static int g12 = 13, g13 = 14, g14 = 15, g15 = 16;

static int
touch(int *value)
{
	return *value;
}

static int
address_pressure(void)
{
	int *p0 = &g0, *p1 = &g1, *p2 = &g2, *p3 = &g3;
	int *p4 = &g4, *p5 = &g5, *p6 = &g6, *p7 = &g7;
	int *p8 = &g8, *p9 = &g9, *p10 = &g10, *p11 = &g11;
	int *p12 = &g12, *p13 = &g13, *p14 = &g14, *p15 = &g15;
	int sum;

	sum = touch(p0);
	return sum + *p0 + *p1 + *p2 + *p3 + *p4 + *p5 + *p6 + *p7 +
	    *p8 + *p9 + *p10 + *p11 + *p12 + *p13 + *p14 + *p15;
}

int
main(void)
{
	return address_pressure() == 137 ? 0 : 1;
}
