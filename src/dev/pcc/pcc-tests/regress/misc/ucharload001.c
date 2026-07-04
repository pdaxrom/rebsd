static volatile unsigned char cookie[4] = { 99U, 130U, 83U, 255U };
static volatile unsigned short words[2] = { 0x8234U, 0xffffU };

int
main(argc, argv)
	int argc;
	char **argv;
{
	unsigned int value;

	value = cookie[1];
	if (value != 130U)
		return 1;
	if (cookie[1] != 130U)
		return 2;
	if (cookie[3] != 255U)
		return 3;

	value = words[0];
	if (value != 0x8234U)
		return 4;
	if (words[1] != 0xffffU)
		return 5;

	return 0;
}
