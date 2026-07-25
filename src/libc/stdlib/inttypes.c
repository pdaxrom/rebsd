#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

intmax_t
imaxabs(intmax_t value)
{
	return value < 0 ? -value : value;
}

imaxdiv_t
imaxdiv(intmax_t numerator, intmax_t denominator)
{
	imaxdiv_t result;

	result.quot = numerator / denominator;
	result.rem = numerator % denominator;
	return result;
}

intmax_t
strtoimax(const char *text, char **end, int base)
{
	return strtoll(text, end, base);
}

uintmax_t
strtoumax(const char *text, char **end, int base)
{
	return strtoull(text, end, base);
}

static int
wide_digit(wchar_t ch)
{
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	if (ch >= L'a' && ch <= L'z')
		return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'Z')
		return ch - L'A' + 10;
	return -1;
}

static uintmax_t
wide_uint(const wchar_t *text, wchar_t **end, int base, int signed_result,
    int *negative_result, int *overflow_result)
{
	const wchar_t *start, *p;
	uintmax_t value, cutoff, limit;
	int digit, negative, overflow;

	start = text;
	while (*text == L' ' || (*text >= L'\t' && *text <= L'\r'))
		text++;
	negative = 0;
	if (*text == L'-' || *text == L'+') {
		negative = *text == L'-';
		text++;
	}
	if ((base == 0 || base == 16) && text[0] == L'0' &&
	    (text[1] == L'x' || text[1] == L'X') &&
	    (digit = wide_digit(text[2])) >= 0 && digit < 16) {
		text += 2;
		base = 16;
	}
	if (base == 0)
		base = *text == L'0' ? 8 : 10;
	if (base < 2 || base > 36) {
		errno = EINVAL;
		if (end != 0)
			*end = (wchar_t *)start;
		*negative_result = negative;
		*overflow_result = 0;
		return 0;
	}

	if (signed_result)
		limit = negative ? 0x8000000000000000ULL :
		    0x7fffffffffffffffULL;
	else
		limit = ~(uintmax_t)0;
	cutoff = limit / (unsigned)base;
	value = 0;
	overflow = 0;
	p = text;
	while ((digit = wide_digit(*p)) >= 0 && digit < base) {
		if (value > cutoff ||
		    (value == cutoff &&
		    (unsigned)digit > (unsigned)(limit % (unsigned)base)))
			overflow = 1;
		else
			value = value * (unsigned)base + (unsigned)digit;
		p++;
	}
	if (end != 0)
		*end = (wchar_t *)(p == text ? start : p);
	*negative_result = negative;
	*overflow_result = overflow;
	if (p == text)
		return 0;
	if (overflow) {
		errno = ERANGE;
		return limit;
	}
	return value;
}

intmax_t
wcstoimax(const wchar_t *text, wchar_t **end, int base)
{
	uintmax_t value;
	int negative, overflow;

	value = wide_uint(text, end, base, 1, &negative, &overflow);
	if (overflow)
		return negative ? INTMAX_MIN : INTMAX_MAX;
	if (negative && value == 0x8000000000000000ULL)
		return INTMAX_MIN;
	return negative ? -(intmax_t)value : (intmax_t)value;
}

uintmax_t
wcstoumax(const wchar_t *text, wchar_t **end, int base)
{
	uintmax_t value;
	int negative, overflow;

	value = wide_uint(text, end, base, 0, &negative, &overflow);
	if (overflow)
		return UINTMAX_MAX;
	return negative ? (uintmax_t)-value : value;
}
