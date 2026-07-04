#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int
fail(const char *tag)
{
	printf("wide-policy-smoke fail: %s\n", tag);
	return 1;
}

static int
check_wide_strings(void)
{
	wchar_t buf[16];
	wchar_t moved[16];
	const wchar_t word[] = { 'R', 'e', 'B', 'S', 'D', 0 };
	const wchar_t tail[] = { '-', 'P', 'C', 'C', 0 };

	if (sizeof(wchar_t) != 2)
		return fail("wchar-size");
	if (wcslen(word) != 5)
		return fail("wcslen");
	wcscpy(buf, word);
	wcsncat(buf, tail, 4);
	if (wcscmp(buf, (const wchar_t []) {
	    'R', 'e', 'B', 'S', 'D', '-', 'P', 'C', 'C', 0 }) != 0)
		return fail("wcscpy-wcsncat");
	if (wcsstr(buf, (const wchar_t []) { 'B', 'S', 'D', 0 }) != buf + 2)
		return fail("wcsstr");
	wmemset(moved, 0, sizeof(moved) / sizeof(moved[0]));
	wmemcpy(moved, buf, 9);
	wmemmove(moved + 1, moved, 8);
	if (moved[0] != 'R' || moved[1] != 'R' || moved[8] != 'C')
		return fail("wmemmove");
	return 0;
}

static int
check_conversion_policy(void)
{
	char mb[16];
	wchar_t wc[16];
	const char *src;
	const wchar_t *wsrc;
	mbstate_t st;
	size_t n;

	memset(&st, 0, sizeof(st));
	if (!mbsinit(&st))
		return fail("mbsinit");
	if (btowc('A') != 'A' || btowc(0x80) != WEOF)
		return fail("btowc");
	if (wctob('Z') != 'Z' || wctob((wint_t)0x80) != EOF)
		return fail("wctob");

	n = mbstowcs(wc, "abc", 16);
	if (n != 3 || wc[0] != 'a' || wc[1] != 'b' || wc[2] != 'c' ||
	    wc[3] != 0)
		return fail("mbstowcs-ascii");
	if (wcstombs(mb, wc, sizeof(mb)) != 3 || strcmp(mb, "abc") != 0)
		return fail("wcstombs-ascii");

	if (mbrtowc(wc, "\200", 1, &st) != (size_t)-1)
		return fail("mbrtowc-nonascii");
	if (wcrtomb(mb, (wchar_t)0x80, &st) != (size_t)-1)
		return fail("wcrtomb-nonascii");
	if (mbstowcs(wc, "\200", 16) != (size_t)-1)
		return fail("mbstowcs-nonascii");
	wc[0] = (wchar_t)0x80;
	wc[1] = 0;
	if (wcstombs(mb, wc, sizeof(mb)) != (size_t)-1)
		return fail("wcstombs-nonascii");

	src = "xy";
	if (mbsrtowcs(wc, &src, 16, &st) != 2 || src != NULL ||
	    wc[0] != 'x' || wc[1] != 'y' || wc[2] != 0)
		return fail("mbsrtowcs");
	wsrc = (const wchar_t []) { 'o', 'k', 0 };
	if (wcsrtombs(mb, &wsrc, sizeof(mb), &st) != 2 || wsrc != NULL ||
	    strcmp(mb, "ok") != 0)
		return fail("wcsrtombs");
	return 0;
}

static int
check_numeric(void)
{
	wchar_t *end;
	const wchar_t num[] = { '1', '2', '3', 'x', 0 };
	const wchar_t fp[] = { '2', '.', '5', 'x', 0 };

	if (wcstol(num, &end, 10) != 123 || *end != 'x')
		return fail("wcstol");
	if (wcstoul(num, &end, 10) != 123 || *end != 'x')
		return fail("wcstoul");
	if (wcstod(fp, &end) != 2.5 || *end != 'x')
		return fail("wcstod");
	if (wcstof(fp, &end) != 2.5f || *end != 'x')
		return fail("wcstof");
	if (wcstold(fp, &end) != (long double)2.5 || *end != 'x')
		return fail("wcstold");
	return 0;
}

int
main(void)
{
	if (check_wide_strings())
		return 1;
	if (check_conversion_policy())
		return 1;
	if (check_numeric())
		return 1;
	printf("wide policy smoke ok\n");
	return 0;
}
