typedef unsigned long ulong;
typedef unsigned long long ullong;

struct abi_rec {
	int tag;
	ullong value;
	int tail;
};

#if defined(__mips32r2) || defined(TARGET_MIPS32R2)
#define EXPECT_ABI_VALUE_OFFSET 4
#else
#define EXPECT_ABI_VALUE_OFFSET 8
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SMOKE_LITTLE_ENDIAN 1
#elif defined(__MIPSEL__) || defined(__mipsel__) || defined(TARGET_LITTLE_ENDIAN)
#define SMOKE_LITTLE_ENDIAN 1
#else
#define SMOKE_LITTLE_ENDIAN 0
#endif

ullong abi_global = 0x1122334455667788ULL;
struct abi_rec abi_record = {
	0x12345678,
	0x0102030405060708ULL,
	0x55667788
};

int asm_check_reg(int marker, ullong value);
int asm_check_stack3(int a, int b, int c, ullong value);
int asm_check_stack4(int a, int b, int c, int d, ullong value);
int asm_call_c_check(void);
int asm_call_c_ret(void);
ullong asm_echo_reg(int marker, ullong value);
ullong asm_ret_ull(void);

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
	putstr("ll abi bad: ");
	putstr(s);
	putstr("\n");
	return 1;
}

int
c_check_reg(marker, value)
	int marker;
	ullong value;
{
	return marker == 0x33 && value == 0x0102030405060708ULL;
}

ullong
c_ret_ull()
{
	return 0x8877665544332211ULL;
}

main()
{
	ulong *p;

	putstr("check global object\n");
	p = (ulong *)&abi_global;
	if (SMOKE_LITTLE_ENDIAN) {
		if (p[0] != 0x55667788UL || p[1] != 0x11223344UL)
			return bad("global object word order");
	} else if (p[0] != 0x11223344UL || p[1] != 0x55667788UL)
		return bad("global object high/low order");

	putstr("check struct object\n");
	if ((int)((char *)&abi_record.value - (char *)&abi_record) !=
	    EXPECT_ABI_VALUE_OFFSET)
		return bad("struct long long alignment");
	p = (ulong *)&abi_record.value;
	if (abi_record.tag != 0x12345678 || abi_record.tail != 0x55667788)
		return bad("struct surrounding fields");
	if (SMOKE_LITTLE_ENDIAN) {
		if (p[0] != 0x05060708UL || p[1] != 0x01020304UL)
			return bad("struct long long word order");
	} else if (p[0] != 0x01020304UL || p[1] != 0x05060708UL)
		return bad("struct long long high/low order");

	putstr("check C to asm register argument\n");
	if (! asm_check_reg(0x11, 0x1122334455667788ULL))
		return bad("C to asm register argument");
	putstr("check asm register return\n");
	if (asm_echo_reg(0x22, 0x0102030405060708ULL) !=
	    0x0102030405060708ULL)
		return bad("asm register return");
	putstr("check stack argument after 3 ints\n");
	if (! asm_check_stack3(1, 2, 3, 0x1122334455667788ULL))
		return bad("C to asm stack argument after 3 ints");
	putstr("check stack argument after 4 ints\n");
	if (! asm_check_stack4(1, 2, 3, 4, 0x8877665544332211ULL))
		return bad("C to asm stack argument after 4 ints");
	putstr("check asm direct return\n");
	if (asm_ret_ull() != 0x8877665544332211ULL)
		return bad("asm direct return");
	putstr("check asm to C register argument\n");
	if (! asm_call_c_check())
		return bad("asm to C register argument");
	putstr("check C register return to asm\n");
	if (! asm_call_c_ret())
		return bad("C register return to asm");

	putstr("long long abi smoke ok\n");
	return 0;
}
