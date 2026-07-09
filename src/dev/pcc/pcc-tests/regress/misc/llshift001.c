/* Results of helper-backed shifts must survive a following helper call. */

volatile unsigned long long left = 1;
volatile unsigned long long right = 0xf0;
volatile unsigned int lcount = 4;
volatile unsigned int rcount = 4;

int
main(void)
{
	unsigned long long result;

	result = (left << lcount) | (right >> rcount);
	return result == 0x1f ? 0 : 1;
}
