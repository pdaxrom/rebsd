static int
check_high(unsigned value)
{
	if (value <= 0xffffU)
		return 1;
	if (value < 0xffff8000U)
		return 2;
	if (!(value >= 0xffff8000U))
		return 3;
	if (!(0xffff8000U <= value))
		return 4;
	if (1U > value)
		return 5;
	if (!(value > 1U))
		return 6;
	return 0;
}

static int
check_zero(unsigned zero, unsigned one)
{
	if (zero > 0U)
		return 10;
	if (!(zero <= 0U))
		return 11;
	if (zero < 0U)
		return 12;
	if (!(zero >= 0U))
		return 13;
	if (!(one > 0U))
		return 14;
	if (one <= 0U)
		return 15;
	return 0;
}

int
main(int argc, char **argv)
{
	volatile unsigned high = ~2U;
	volatile unsigned zero = 0;
	volatile unsigned one = 1;
	int rc;

	rc = check_high(high);
	if (rc != 0)
		return rc;
	return check_zero(zero, one);
}
