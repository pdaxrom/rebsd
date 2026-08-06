struct word {
	unsigned value;
};

struct pair {
	unsigned first;
	unsigned second;
};

static unsigned
unpack(value)
	struct word value;
{
	return value.value;
}

static unsigned
relay(value)
	struct word value;
{
	unsigned (*fn)();

	fn = unpack;
	return (*fn)(value);
}

static struct pair
make_pair(value)
	unsigned value;
{
	struct pair result;

	result.first = value;
	result.second = value ^ 0x55aa55aaU;
	return result;
}

static struct pair
relay_pair(value)
	unsigned value;
{
	struct pair (*fn)();

	fn = make_pair;
	return (*fn)(value);
}

static long long
divide(value, divisor)
	long long value;
	int divisor;
{
	return value / divisor;
}

main()
{
	long long value;
	double fp;
	struct word word;
	struct pair pair;

	value = divide(864197523LL, 9);
	fp = 1.5 * 4.0;
	word.value = 0x12345678U;
	pair = relay_pair(0x13579bdfU);

	if (value != 96021947LL || fp != 6.0 ||
	    relay(word) != 0x12345678U || pair.first != 0x13579bdfU ||
	    pair.second != (0x13579bdfU ^ 0x55aa55aaU))
		return 1;
	write(1, "pcc smoke ok\n", 13);
	return 0;
}
