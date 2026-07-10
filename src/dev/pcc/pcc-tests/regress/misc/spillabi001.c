#include <stdarg.h>

struct item {
	int tag;
	double value;
};

static struct item
copy_item(struct item value)
{
	struct item copy = value;

	copy.tag += 3;
	copy.value += 0.5;
	return copy;
}

static double
sum_items(int count, ...)
{
	va_list ap;
	double sum = 0.0;
	int i;

	va_start(ap, count);
	for (i = 0; i < count; i++)
		sum += va_arg(ap, double);
	va_end(ap);
	return sum;
}

int
main(void)
{
	struct item first = { 4, 1.25 };
	struct item second = copy_item(first);
	double sum = sum_items(4, first.value, second.value, 2.0, 3.0);

	return second.tag == 7 && second.value == 1.75 && sum == 8.0 ? 0 : 1;
}
