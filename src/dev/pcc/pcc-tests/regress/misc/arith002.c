static volatile unsigned one = 1U;
static volatile unsigned top = (~0U >> 1) + 1U;

int
main(int argc, char **argv)
{
	unsigned x, y;

	x = one - top;
	if (x != top + one)
		return 1;

	y = -top;
	if (y != top)
		return 2;

	return 0;
}
