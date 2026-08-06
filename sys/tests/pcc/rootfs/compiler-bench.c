/*
 * Small compiler-oriented benchmark corpus for ReBSD.
 *
 * Each kernel isolates a common code-generation class.  The benchmark is
 * intentionally self-contained so the same source can be built, linked, and
 * timed with GCC and PCC, including each compiler's libc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ARRAY_COUNT 256
#define BYTE_COUNT 1024

typedef unsigned long long u64;

static unsigned int input_a[ARRAY_COUNT];
static unsigned int input_b[ARRAY_COUNT];
static unsigned int output_a[ARRAY_COUNT];
static float input_f[ARRAY_COUNT];
static double input_d[ARRAY_COUNT];
static unsigned char bytes_a[BYTE_COUNT];
static unsigned char bytes_b[BYTE_COUNT];

static volatile u64 result_sink;
static volatile double fp_sink;

typedef u64 (*bench_fn)(unsigned int);

struct benchmark {
	const char *name;
	bench_fn function;
	unsigned int work_per_rep;
	u64 expected;
};

static double
now_seconds(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void
initialize_inputs(void)
{
	unsigned int i;
	unsigned int x;

	x = 0x13579bdfU;
	for (i = 0; i < ARRAY_COUNT; i++) {
		x = x * 1664525U + 1013904223U;
		input_a[i] = x;
		x = x * 1664525U + 1013904223U;
		input_b[i] = x;
		input_f[i] = (float)((int)(x & 0xffffU) - 32768) / 32768.0f;
		input_d[i] = (double)((int)((x >> 8) & 0xffffU) - 32768) /
		    32768.0;
	}
	for (i = 0; i < BYTE_COUNT; i++) {
		bytes_a[i] = (unsigned char)(input_a[i & 255] >>
		    ((i & 3) * 8));
		bytes_b[i] = 0;
	}
}

static u64
bench_int_mix(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;
	unsigned int x;

	acc = 0x2468ace0U;
	x = 0x12345678U;
	for (r = 0; r < reps; r++) {
		x ^= r + 0x9e3779b9U;
		for (i = 0; i < ARRAY_COUNT; i++) {
			x += input_a[i];
			x = x * 33U + 17U;
			x ^= x >> 13;
			x += x << 7;
			acc += x ^ (x >> 16);
		}
	}
	return ((u64)acc << 32) | x;
}

static u64
bench_const_div(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;
	unsigned int x;
	int y;

	acc = 0x31415926U;
	for (r = 0; r < reps; r++) {
		for (i = 0; i < ARRAY_COUNT; i++) {
			x = input_a[i] + r;
			acc += x / 3U;
			acc ^= x % 7U;
			acc += x / 10U;
			acc ^= x % 31U;
			y = (int)(x & 0x7fffffffU);
			acc += (unsigned int)(y / 13);
			acc ^= (unsigned int)(y % 17);
		}
	}
	return ((u64)acc << 32) | (acc ^ reps);
}

static u64
bench_branch(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;
	unsigned int v;

	acc = 0;
	for (r = 0; r < reps; r++) {
		for (i = 0; i < ARRAY_COUNT; i++) {
			v = input_a[i] ^ r;
			if (v & 1U)
				acc += v;
			else
				acc ^= v >> 3;
			if ((v & 0xffU) < 93U)
				acc += input_b[i];
			else if (v & 0x10000U)
				acc -= input_b[i] >> 5;
			else
				acc ^= input_b[i] << 3;
		}
	}
	return ((u64)acc << 32) | (acc + reps);
}

static u64
bench_switch(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;
	unsigned int v;

	acc = 0xabcdef01U;
	for (r = 0; r < reps; r++) {
		for (i = 0; i < ARRAY_COUNT; i++) {
			v = input_a[i];
			switch ((v >> 17) & 7U) {
			case 0: acc += v; break;
			case 1: acc ^= v >> 7; break;
			case 2: acc -= v << 3; break;
			case 3: acc += v ^ input_b[i]; break;
			case 4: acc = (acc << 5) | (acc >> 27); break;
			case 5: acc += v >> (i & 7); break;
			case 6: acc ^= input_b[i] << (i & 3); break;
			default: acc += 0x9e3779b9U + r; break;
			}
		}
	}
	return ((u64)acc << 32) | (acc ^ 0x55aa55aaU);
}

static u64
bench_memory(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;

	acc = 0;
	for (r = 0; r < reps; r++) {
		for (i = 0; i < ARRAY_COUNT; i++)
			output_a[i] = input_a[i] + (input_b[i] ^ r);
		for (i = 0; i < ARRAY_COUNT; i++)
			acc += output_a[(i * 17U) & 255U] ^ i;
	}
	return ((u64)acc << 32) | output_a[127];
}

static u64
bench_libc_memory(unsigned int reps)
{
	unsigned int acc;
	unsigned int r;

	acc = 0;
	for (r = 0; r < reps; r++) {
		memcpy(bytes_b, bytes_a, BYTE_COUNT);
		memset(bytes_b + 256, (int)(r & 255U), 256);
		acc += (unsigned int)(unsigned char)bytes_b[r & 255U];
		acc += (unsigned int)(memcmp(bytes_a, bytes_b, 128) == 0);
	}
	return ((u64)acc << 32) | bytes_b[511];
}

static unsigned int
call_leaf(unsigned int a, unsigned int b, unsigned int c)
{
	a += b ^ (c << 3);
	a = (a << 9) | (a >> 23);
	return a * 9U + 1U;
}

static unsigned int (*volatile indirect_leaf)(unsigned int, unsigned int,
    unsigned int) = call_leaf;

static u64
bench_calls(unsigned int reps)
{
	unsigned int acc;
	unsigned int i;
	unsigned int r;

	acc = 0x10203040U;
	for (r = 0; r < reps; r++)
		for (i = 0; i < ARRAY_COUNT; i++)
			acc = indirect_leaf(acc, input_a[i], r + i);
	return ((u64)acc << 32) | (acc + reps);
}

static u64
bench_u64(unsigned int reps)
{
	u64 acc;
	u64 x;
	unsigned int i;
	unsigned int r;

	acc = 0x0123456789abcdefULL;
	for (r = 0; r < reps; r++) {
		x = acc ^ r;
		for (i = 0; i < ARRAY_COUNT; i++) {
			x += ((u64)input_a[i] << 32) | input_b[i];
			x ^= x >> 11;
			x = (x << 17) | (x >> 47);
			acc += x ^ (u64)i;
		}
	}
	return acc ^ x;
}

static u64
double_bits(double value)
{
	union {
		double d;
		u64 u;
	} bits;

	bits.d = value;
	return bits.u;
}

static u64
bench_float(unsigned int reps)
{
	float acc;
	float scale;
	unsigned int i;
	unsigned int r;

	acc = 0.125f;
	for (r = 0; r < reps; r++) {
		scale = 0.75f + (float)(r & 7U) * 0.03125f;
		for (i = 0; i < ARRAY_COUNT; i++)
			acc += input_f[i] * scale + input_f[255U - i] * 0.125f;
		acc *= 0.9995f;
	}
	fp_sink = acc;
	return double_bits((double)acc);
}

static u64
bench_double(unsigned int reps)
{
	double acc;
	double scale;
	unsigned int i;
	unsigned int r;

	acc = 0.25;
	for (r = 0; r < reps; r++) {
		scale = 0.5 + (double)(r & 15U) * 0.015625;
		for (i = 0; i < ARRAY_COUNT; i++)
			acc += input_d[i] * input_d[255U - i] +
			    input_d[i] * scale;
		acc *= 0.99995;
	}
	fp_sink = acc;
	return double_bits(acc);
}

static u64
bench_convert(unsigned int reps)
{
	double acc;
	unsigned int i;
	unsigned int r;
	unsigned int sum;
	unsigned int value;
	int converted;

	acc = 0.0;
	sum = 0;
	for (r = 0; r < reps; r++) {
		for (i = 0; i < ARRAY_COUNT; i++) {
			value = (input_a[i] >> 9) & 0x003fffffU;
			acc += (double)value * 0.125;
			converted = (int)(acc * 0.00000095367431640625);
			sum += (unsigned int)converted;
			if (acc > 1000000000.0)
				acc *= 0.0001;
		}
	}
	fp_sink = acc;
	return ((u64)sum << 32) ^ double_bits(acc);
}

/* Expected checksums are for one repetition after initialize_inputs(). */
static struct benchmark benchmarks[] = {
	{ "int_mix", bench_int_mix, ARRAY_COUNT, 0x66c633995ad4e5b3ULL },
	{ "const_div", bench_const_div, ARRAY_COUNT, 0x889d8e48889d8e49ULL },
	{ "branch", bench_branch, ARRAY_COUNT, 0xf21872ddf21872deULL },
	{ "switch", bench_switch, ARRAY_COUNT, 0x57f9a0ad0253f507ULL },
	{ "memory", bench_memory, ARRAY_COUNT * 2U, 0xb0cb6b80f99c4c5fULL },
	{ "libc_memory", bench_libc_memory, BYTE_COUNT + 256U,
	    0x000000b300000000ULL },
	{ "calls", bench_calls, ARRAY_COUNT, 0x88f3c86f88f3c870ULL },
	{ "u64", bench_u64, ARRAY_COUNT, 0x4603e7afc4929a9cULL },
	{ "float", bench_float, ARRAY_COUNT, 0xc01f87f640000000ULL },
	{ "double", bench_double, ARRAY_COUNT, 0xc001371be910240bULL },
	{ "convert", bench_convert, ARRAY_COUNT, 0x418e699301000000ULL },
};

static int
self_test(void)
{
	unsigned int i;
	u64 actual;
	int failures;

	failures = 0;
	for (i = 0; i < sizeof(benchmarks) / sizeof(benchmarks[0]); i++) {
		actual = benchmarks[i].function(1);
		printf("COMPILER_BENCH_SELFTEST_VALUE %s %08x%08x\n",
		    benchmarks[i].name, (unsigned int)(actual >> 32),
		    (unsigned int)actual);
		if (benchmarks[i].expected != 0ULL &&
		    actual != benchmarks[i].expected) {
			printf("COMPILER_BENCH_SELFTEST_FAIL %s "
			    "expected=%08x%08x actual=%08x%08x\n",
			    benchmarks[i].name,
			    (unsigned int)(benchmarks[i].expected >> 32),
			    (unsigned int)benchmarks[i].expected,
			    (unsigned int)(actual >> 32), (unsigned int)actual);
			failures++;
		}
		result_sink ^= actual;
	}
	return failures;
}

static void
run_benchmark(const struct benchmark *bench, double min_seconds)
{
	double elapsed;
	double rate;
	double start;
	u64 checksum;
	unsigned int reps;

	reps = 1;
	for (;;) {
		start = now_seconds();
		checksum = bench->function(reps);
		elapsed = now_seconds() - start;
		result_sink ^= checksum;
		if (elapsed >= min_seconds || reps >= 0x40000000U)
			break;
		reps *= 2;
	}
	if (elapsed > 0.0)
		rate = ((double)bench->work_per_rep * (double)reps) /
		    elapsed / 1000000.0;
	else
		rate = 0.0;
	printf("COMPILER_BENCH_RESULT %s reps=%u seconds=%.6f "
	    "mwork_s=%.3f checksum=%08x%08x\n", bench->name, reps, elapsed,
	    rate, (unsigned int)(checksum >> 32), (unsigned int)checksum);
}

int
main(void)
{
	const char *value;
	double min_seconds;
	unsigned int i;
	int failures;

	min_seconds = 0.25;
	value = getenv("COMPILER_BENCH_MIN_SECONDS");
	if (value != NULL && *value != '\0')
		min_seconds = atof(value);
	if (min_seconds < 0.0)
		min_seconds = 0.0;

	initialize_inputs();
	result_sink = 0;
	fp_sink = 0.0;
	printf("COMPILER_BENCH_BEGIN min_seconds=%.3f\n", min_seconds);
	failures = self_test();
	printf("COMPILER_BENCH_SELFTEST %d\n", failures);
	if (failures == 0)
		for (i = 0; i < sizeof(benchmarks) / sizeof(benchmarks[0]); i++)
			run_benchmark(&benchmarks[i], min_seconds);
	printf("COMPILER_BENCH_SINK %08x%08x %.9e\n",
	    (unsigned int)(result_sink >> 32), (unsigned int)result_sink,
	    fp_sink);
	printf("COMPILER_BENCH_END %d\n", failures);
	return failures != 0;
}
