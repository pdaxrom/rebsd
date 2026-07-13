/* A two-word result must not partially overlap either two-word input. */

#define COUNT 256

typedef unsigned long long u64;

static unsigned int high_words[COUNT];
static unsigned int low_words[COUNT];

static u64
mix_words(unsigned int reps)
{
	u64 acc;
	u64 value;
	unsigned int i;
	unsigned int r;

	acc = 0x0123456789abcdefULL;
	for (r = 0; r < reps; r++) {
		value = acc ^ r;
		for (i = 0; i < COUNT; i++) {
			value += ((u64)high_words[i] << 32) | low_words[i];
			value ^= value >> 11;
			value = (value << 17) | (value >> 47);
			acc += value ^ (u64)i;
		}
	}
	return acc ^ value;
}

int
main(void)
{
	unsigned int i;
	unsigned int seed;

	seed = 0x13579bdfU;
	for (i = 0; i < COUNT; i++) {
		seed = seed * 1664525U + 1013904223U;
		high_words[i] = seed;
		seed = seed * 1664525U + 1013904223U;
		low_words[i] = seed;
	}
	return mix_words(1) == 0x4603e7afc4929a9cULL ? 0 : 1;
}
