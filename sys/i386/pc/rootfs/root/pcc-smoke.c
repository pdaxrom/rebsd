#include <stdio.h>

struct word {
	unsigned value;
};

struct pair {
	unsigned first;
	unsigned second;
};

static unsigned
unpack(struct word value)
{
	return value.value;
}

static unsigned
relay(struct word value)
{
	unsigned (*fn)(struct word) = unpack;

	return fn(value);
}

static struct pair
make_pair(unsigned value)
{
	struct pair result = { value, value ^ 0x55aa55aaU };

	return result;
}

static struct pair
relay_pair(unsigned value)
{
	struct pair (*fn)(unsigned) = make_pair;

	return fn(value);
}

static long long
divide(long long value, int divisor)
{
	return value / divisor;
}

int
main(void)
{
	long long value = divide(864197523LL, 9);
	double fp = 1.5 * 4.0;
	struct word word = { 0x12345678U };
	struct pair pair = relay_pair(0x13579bdfU);

	if (sizeof(long double) != 8 || value != 96021947LL || fp != 6.0 ||
	    relay(word) != 0x12345678U || pair.first != 0x13579bdfU ||
	    pair.second != (0x13579bdfU ^ 0x55aa55aaU))
		return 1;
	printf("i686 pcc smoke ok\n");
	return 0;
}
