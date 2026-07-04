typedef long long quad_t;
typedef unsigned long long u_quad_t;
typedef unsigned int qshift_t;

quad_t __ashldi3(quad_t, qshift_t);
quad_t __ashrdi3(quad_t, qshift_t);
quad_t __divdi3(quad_t, quad_t);
quad_t __fixdfdi(double);
u_quad_t __fixunsdfdi(double);
double __floatdidf(quad_t);
double __floatunsdidf(u_quad_t);
quad_t __lshrdi3(quad_t, qshift_t);
quad_t __moddi3(quad_t, quad_t);
quad_t __muldi3(quad_t, quad_t);
u_quad_t __udivdi3(u_quad_t, u_quad_t);
u_quad_t __umoddi3(u_quad_t, u_quad_t);

putstr(s)
	char *s;
{
	char *p;

	for (p = s; *p; p++)
		;
	write(1, s, p - s);
}

bad(s)
	char *s;
{
	putstr("libpcc helper bad: ");
	putstr(s);
	putstr("\n");
	return 1;
}

int
near_double(a, b, e)
	double a;
	double b;
	double e;
{
	double d;

	d = a - b;
	if (d < 0.0)
		d = -d;
	return d <= e;
}

int
check_shift_helpers()
{
	if ((u_quad_t)__ashldi3(1LL, 0) != 1ULL)
		return bad("ashl 0");
	if ((u_quad_t)__ashldi3(1LL, 31) != 0x80000000ULL)
		return bad("ashl 31");
	if ((u_quad_t)__ashldi3(1LL, 32) != 0x100000000ULL)
		return bad("ashl 32");
	if ((u_quad_t)__ashldi3(1LL, 63) != 0x8000000000000000ULL)
		return bad("ashl 63");
	if ((u_quad_t)__ashldi3(0x1234LL, 64) != 0ULL)
		return bad("ashl 64");
	if ((u_quad_t)__ashldi3(0x1234LL, 65) != 0ULL)
		return bad("ashl 65");

	if ((u_quad_t)__lshrdi3((quad_t)0x8000000000000000ULL, 63) != 1ULL)
		return bad("lshr 63");
	if ((u_quad_t)__lshrdi3((quad_t)0x0123456789abcdefULL, 32) !=
	    0x01234567ULL)
		return bad("lshr 32");
	if ((u_quad_t)__lshrdi3((quad_t)0x0123456789abcdefULL, 64) != 0ULL)
		return bad("lshr 64");
	if ((u_quad_t)__lshrdi3((quad_t)0x0123456789abcdefULL, 65) != 0ULL)
		return bad("lshr 65");

	if (__ashrdi3(-1LL, 64) != -1LL)
		return bad("ashr -1 64");
	if (__ashrdi3(-1LL, 65) != -1LL)
		return bad("ashr -1 65");
	if (__ashrdi3(-0x100000000LL, 32) != -1LL)
		return bad("ashr negative 32");
	if (__ashrdi3(0x7000000000000000LL, 63) != 0LL)
		return bad("ashr positive 63");

	return 0;
}

int
check_divmod_helpers()
{
	quad_t sq, sr;
	u_quad_t un, ud, uq, ur;

	if (__divdi3(-123456789LL, 12345LL) != -10000LL)
		return bad("sdiv negative numerator");
	if (__moddi3(-123456789LL, 12345LL) != -6789LL)
		return bad("smod negative numerator");
	if (__divdi3(123456789LL, -12345LL) != -10000LL)
		return bad("sdiv negative denominator");
	if (__moddi3(123456789LL, -12345LL) != 6789LL)
		return bad("smod negative denominator");
	if (__divdi3(-123456789LL, -12345LL) != 10000LL)
		return bad("sdiv both negative");
	if (__moddi3(-123456789LL, -12345LL) != -6789LL)
		return bad("smod both negative");

	sq = __divdi3(-0x123456789LL, 0x12345LL);
	sr = __moddi3(-0x123456789LL, 0x12345LL);
	if (sq * 0x12345LL + sr != -0x123456789LL)
		return bad("signed divmod identity");
	if (sr > 0)
		return bad("signed remainder sign");

	un = 0x123456789abcdef0ULL;
	ud = 0x12345ULL;
	uq = __udivdi3(un, ud);
	ur = __umoddi3(un, ud);
	if (uq * ud + ur != un)
		return bad("unsigned divmod identity");
	if (ur >= ud)
		return bad("unsigned remainder range");

	if ((u_quad_t)__muldi3(0x12345LL, 0x10001LL) != 0x123462345ULL)
		return bad("mul helper");

	return 0;
}

int
check_conversion_helpers()
{
	if (__fixdfdi(4294967296.0) != 0x100000000LL)
		return bad("fixdfdi positive");
	if (__fixdfdi(-4294967296.0) != -0x100000000LL)
		return bad("fixdfdi negative");
	if (__fixunsdfdi(1099511627776.0) != 0x10000000000ULL)
		return bad("fixunsdfdi positive");

	if (!near_double(__floatdidf(0x100000000LL), 4294967296.0, 0.5))
		return bad("floatdidf positive");
	if (!near_double(__floatdidf(-0x100000000LL), -4294967296.0, 0.5))
		return bad("floatdidf negative");
	if (!near_double(__floatunsdidf(0x10000000000ULL), 1099511627776.0,
	    0.5))
		return bad("floatunsdidf positive");

	return 0;
}

main()
{
	int rc;

	rc = check_shift_helpers();
	if (rc != 0)
		return rc;
	rc = check_divmod_helpers();
	if (rc != 0)
		return rc;
	rc = check_conversion_helpers();
	if (rc != 0)
		return rc;

	putstr("libpcc helper smoke ok\n");
	return 0;
}
