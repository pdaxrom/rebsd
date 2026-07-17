static void
pointer_copy(unsigned char *destination, const unsigned char *source,
    unsigned int count)
{
	unsigned char *to;
	const unsigned char *from;

	to = destination;
	from = source;
	while (count-- > 0)
		*to++ = *from++;
}

static void
pointer_fill(unsigned char *destination, unsigned char value,
    unsigned int count)
{
	unsigned char *to;

	to = destination;
	while (count-- > 0)
		*to++ = value;
}

static int
pointer_compare(const unsigned char *left, const unsigned char *right,
    unsigned int count)
{
	const unsigned char *a;
	const unsigned char *b;

	a = left;
	b = right;
	while (count-- > 0) {
		if (*a != *b)
			return (int)*a - (int)*b;
		a++;
		b++;
	}
	return 0;
}

static void
pointer_retarget(int **slot, int *value)
{
	*slot = value;
}

static int
pointer_address_taken(int *first, int *second)
{
	int *pointer;

	pointer = first;
	pointer_retarget(&pointer, second);
	return *pointer;
}

static int
pointer_add_seven(int value)
{
	return value + 7;
}

static int
pointer_indirect(int (*function)(int), int value)
{
	int (*local_function)(int);

	local_function = function;
	return local_function(value);
}

struct pointer_symbol_entry {
	int first;
	int second;
	int third;
};

static int pointer_symbol_gate;
static const struct pointer_symbol_entry pointer_symbol_entries[] = {
	{ 2, 3, 5 },
	{ 7, 11, 13 },
	{ 17, 19, 23 },
};

static int
pointer_symbol_select(unsigned int index)
{
	const struct pointer_symbol_entry *entry;

	entry = &pointer_symbol_entries[index];
	if (pointer_symbol_gate != 0)
		return entry->second + pointer_symbol_gate;
	return entry->third;
}

int
main(void)
{
	unsigned char source[32];
	unsigned char destination[32];
	int first, second;
	unsigned int i;

	for (i = 0; i < 32; i++) {
		source[i] = (unsigned char)(i * 3U + 1U);
		destination[i] = 0;
	}
	pointer_copy(destination, source, 32);
	if (pointer_compare(destination, source, 32) != 0)
		return 1;
	pointer_fill(destination + 8, 0x5a, 12);
	for (i = 0; i < 32; i++) {
		unsigned char expected;

		expected = i >= 8 && i < 20 ? 0x5a : source[i];
		if (destination[i] != expected)
			return 2;
	}
	destination[3]++;
	if (pointer_compare(destination, source, 8) != 1)
		return 3;
	destination[3]--;
	if (pointer_compare(destination, source, 8) != 0)
		return 4;
	first = 11;
	second = 29;
	if (pointer_address_taken(&first, &second) != 29)
		return 5;
	if (pointer_indirect(pointer_add_seven, 13) != 20)
		return 6;
	pointer_symbol_gate = 0;
	if (pointer_symbol_select(2) != 23)
		return 7;
	pointer_symbol_gate = 5;
	if (pointer_symbol_select(1) != 16)
		return 8;
	return 0;
}
