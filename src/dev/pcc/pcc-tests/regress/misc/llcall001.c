/* Values live across emulated 64-bit division must survive the helper call. */

typedef unsigned long long u64;

struct result {
	unsigned int qhi;
	unsigned int qlo;
	unsigned int rhi;
	unsigned int rlo;
};

static void
divide_store(u64 numerator, u64 denominator, struct result *out)
{
	u64 quotient;
	u64 remainder;

	quotient = numerator / denominator;
	remainder = numerator % denominator;
	out->qhi = (unsigned int)(quotient >> 32);
	out->qlo = (unsigned int)quotient;
	out->rhi = (unsigned int)(remainder >> 32);
	out->rlo = (unsigned int)remainder;
}

static int
check(u64 numerator, u64 denominator, unsigned int qhi, unsigned int qlo,
    unsigned int rhi, unsigned int rlo)
{
	struct result result;

	divide_store(numerator, denominator, &result);
	return result.qhi == qhi && result.qlo == qlo &&
	    result.rhi == rhi && result.rlo == rlo;
}

int
main(void)
{
	if (!check(0xfedcba9876543210ULL, 0x12345678ULL,
	    0x0000000eU, 0x00000077U, 0U, 0x48U))
		return 1;
	if (!check(0x0123456789abcdefULL, 0x00010001ULL,
	    0x00000123U, 0x44444567U, 0U, 0x8888U))
		return 2;
	if (!check(0xffffffffffffffffULL, 0xfffffffbULL,
	    0x00000001U, 0x00000005U, 0U, 0x18U))
		return 3;
	return 0;
}
