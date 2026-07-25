#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

long long
strtoll(const char *text, char **end, int base)
{
	const char *start, *p;
	unsigned long long value, cutoff, limit;
	int digit, negative, overflow;

	start = text;
	while (isspace((unsigned char)*text))
		text++;
	negative = 0;
	if (*text == '-' || *text == '+') {
		negative = *text == '-';
		text++;
	}
	if ((base == 0 || base == 16) && text[0] == '0' &&
	    (text[1] == 'x' || text[1] == 'X') &&
	    (isdigit((unsigned char)text[2]) ||
	    (tolower((unsigned char)text[2]) >= 'a' &&
	    tolower((unsigned char)text[2]) <= 'f'))) {
		text += 2;
		base = 16;
	}
	if (base == 0)
		base = *text == '0' ? 8 : 10;
	if (base < 2 || base > 36) {
		errno = EINVAL;
		if (end != 0)
			*end = (char *)start;
		return 0;
	}

	limit = negative ? 0x8000000000000000ULL : 0x7fffffffffffffffULL;
	cutoff = limit / (unsigned)base;
	value = 0;
	overflow = 0;
	p = text;
	for (;;) {
		unsigned char ch;

		ch = (unsigned char)*p;
		if (isdigit(ch))
			digit = ch - '0';
		else if (isalpha(ch))
			digit = (tolower(ch) - 'a') + 10;
		else
			break;
		if (digit >= base)
			break;
		if (value > cutoff ||
		    (value == cutoff &&
		    (unsigned)digit > (unsigned)(limit % (unsigned)base)))
			overflow = 1;
		else
			value = value * (unsigned)base + (unsigned)digit;
		p++;
	}
	if (end != 0)
		*end = (char *)(p == text ? start : p);
	if (p == text)
		return 0;
	if (overflow) {
		errno = ERANGE;
		return negative ? (-0x7fffffffffffffffLL - 1) :
		    0x7fffffffffffffffLL;
	}
	if (negative && value == 0x8000000000000000ULL)
		return -0x7fffffffffffffffLL - 1;
	return negative ? -(long long)value : (long long)value;
}
