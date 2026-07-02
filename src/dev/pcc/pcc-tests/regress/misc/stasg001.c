struct item {
	char *name;
	unsigned short len;
	unsigned short flags;
	unsigned value;
};

static char marker[] = "cookie";

static int
consume(void *cookie, struct item *copy)
{
	if (cookie != marker)
		return 1;
	if (copy->name != marker)
		return 2;
	if (copy->len != 6)
		return 3;
	if (copy->flags != 0x20)
		return 4;
	if (copy->value != 0x12345678U)
		return 5;
	return 0;
}

static int
check_narrowing(void)
{
	volatile unsigned short us = 0x0102U;
	volatile short ss = (short)0x81feU;
	volatile unsigned ui = 0x01020304U;
	volatile unsigned long long ull = 0x0102030405060708ULL;

	if ((unsigned char)us != 0x02U)
		return 10;
	if ((signed char)ss != (signed char)0xfe)
		return 11;
	if ((unsigned char)ui != 0x04U)
		return 12;
	if ((unsigned short)ui != 0x0304U)
		return 13;
	if ((unsigned char)ull != 0x08U)
		return 14;
	if ((unsigned short)ull != 0x0708U)
		return 15;
	if ((unsigned)ull != 0x05060708U)
		return 16;
	return 0;
}

static int
copy_then_consume(struct item *src, void *cookie)
{
	struct item copy;

	copy = *src;
	copy.flags &= ~0x80U;
	return consume(cookie, &copy);
}

int
main(int argc, char **argv)
{
	struct item src;
	int rc;

	src.name = marker;
	src.len = 6;
	src.flags = 0xa0;
	src.value = 0x12345678U;

	rc = copy_then_consume(&src, marker);
	if (rc != 0)
		return rc;
	return check_narrowing();
}
