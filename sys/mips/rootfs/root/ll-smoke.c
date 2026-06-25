typedef unsigned long ulong;
typedef long long llong;
typedef unsigned long long ullong;

struct llrec {
	int a;
	llong b;
	int c;
};

llong gs = 0x1122334455667788LL;
ullong gu = 0x8877665544332211ULL;
struct llrec gr = { 0x12345678, 0x1122334455667788LL, 0x55667788 };

putstr(s)
	char *s;
{
	char *p;

	for (p = s; *p; p++)
		;
	write(1, s, p - s);
}

puthex(v)
	ulong v;
{
	static char digits[] = "0123456789abcdef";
	char buf[10];
	int i;

	buf[0] = '0';
	buf[1] = 'x';
	for (i = 0; i < 8; i++)
		buf[i + 2] = digits[(v >> ((7 - i) * 4)) & 15];
	write(1, buf, sizeof(buf));
}

bad(s)
	char *s;
{
	putstr("ll bad: ");
	putstr(s);
	putstr("\n");
	return 1;
}

badwords(s, p)
	char *s;
	ulong *p;
{
	putstr("ll bad: ");
	putstr(s);
	putstr(" w0=");
	puthex(p[0]);
	putstr(" w1=");
	puthex(p[1]);
	putstr("\n");
	return 1;
}

llong
ret_ll()
{
	return 0x1122334455667788LL;
}

ullong
ret_ull()
{
	return 0x8877665544332211ULL;
}

ullong
add_ull(a, b)
	ullong a;
	ullong b;
{
	return a + b;
}

int
check_mixed_reg_args(a, b)
	int a;
	ullong b;
{
	return a == 0x11 && b == 0x0102030405060708ULL;
}

int
check_stack_int(a, b, c, d, e)
	int a;
	int b;
	int c;
	int d;
	int e;
{
	return a == 1 && b == 2 && c == 3 && d == 4 && e == 0x55667788;
}

int
check_stack_ull_after3(a, b, c, d)
	int a;
	int b;
	int c;
	ullong d;
{
	return a == 1 && b == 2 && c == 3 &&
	    d == 0x1122334455667788ULL;
}

int
check_stack_ull_after4(a, b, c, d, e)
	int a;
	int b;
	int c;
	int d;
	ullong e;
{
	return a == 1 && b == 2 && c == 3 && d == 4 &&
	    e == 0x8877665544332211ULL;
}

int
check_stack_double(a, b, c, d, e)
	int a;
	int b;
	int c;
	int d;
	double e;
{
	return a == 1 && b == 2 && c == 3 && d == 4 &&
	    e > 3.249 && e < 3.251;
}

main()
{
	llong s;
	ullong u, q, den, num, prod;
	struct llrec r;

	s = gs;
	u = gu;
	if ((ulong)(ullong)s != 0x55667788UL)
		return badwords("global signed low", (ulong *)&s);
	if ((long)(s >> 32) != 0x11223344L)
		return badwords("global signed high", (ulong *)&s);
	if ((ulong)u != 0x44332211UL)
		return badwords("global unsigned low", (ulong *)&u);
	if ((ulong)(u >> 32) != 0x88776655UL)
		return badwords("global unsigned high", (ulong *)&u);

	s = -0x0000000100000000LL;
	if ((long)(s >> 32) != -1L)
		return bad("signed shift right");
	if ((ulong)(ullong)s != 0)
		return bad("signed low zero");
	if (s >= 0)
		return bad("signed compare");

	u = 1ULL;
	u = (u << 32) + 5ULL;
	if ((ulong)(u >> 32) != 1UL || (ulong)u != 5UL)
		return bad("left/right shift");

	q = 123456789ULL;
	den = 12345ULL;
	num = q * den + 67ULL;
	if (num / den != q)
		return bad("unsigned divide");
	if (num % den != 67ULL)
		return bad("unsigned modulo");

	prod = 0x12345ULL * 0x10001ULL;
	if (prod != 0x123462345ULL)
		return bad("unsigned multiply");

	if (add_ull(0x100000000ULL, 0x22ULL) != 0x100000022ULL)
		return bad("arg add");
	if (ret_ll() != 0x1122334455667788LL)
		return bad("return signed");
	if (ret_ull() != 0x8877665544332211ULL)
		return bad("return unsigned");
	if (! check_mixed_reg_args(0x11, 0x0102030405060708ULL))
		return bad("mixed register args");
	if (! check_stack_int(1, 2, 3, 4, 0x55667788))
		return bad("stack int arg");
	if (! check_stack_ull_after3(1, 2, 3, 0x1122334455667788ULL))
		return bad("stack ull arg after 3");
	if (! check_stack_ull_after4(1, 2, 3, 4, 0x8877665544332211ULL))
		return bad("stack ull arg after 4");
	if (! check_stack_double(1, 2, 3, 4, 3.25))
		return bad("stack double arg");

	r = gr;
	if (sizeof(struct llrec) != 24)
		return bad("struct size");
	if ((int)((char *)&r.b - (char *)&r) != 8)
		return bad("struct offset");
	if (r.a != 0x12345678 ||
	    r.b != 0x1122334455667788LL ||
	    r.c != 0x55667788)
		return bad("struct values");

	putstr("long long smoke ok\n");
	return 0;
}
