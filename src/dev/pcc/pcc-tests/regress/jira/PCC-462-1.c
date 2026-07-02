/*
 * ReBSD/MIPS regression for mixed long long bitfields.  The original
 * PCC-462 check only tested f0 > f1, which still passes when f0 is
 * incorrectly read back as 0 and f1 is -2.
 */

struct S_LONGLONG {
	unsigned long long f0 : 7;
	signed long long f1 : 7;
};

struct S_CHAR_LONGLONG {
	unsigned char f0 : 7;
	signed long long f1 : 7;
};

struct S_SHORT_LONGLONG {
	unsigned short f0 : 7;
	signed long long f1 : 7;
};

struct S_INT_LONGLONG {
	unsigned int f0 : 7;
	signed long long f1 : 7;
};

struct S_LONGLONG g_ll = {2, -2};
struct S_CHAR_LONGLONG g_cll = {2, -2};
struct S_SHORT_LONGLONG g_sll = {2, -2};
struct S_INT_LONGLONG g_ill = {2, -2};

#define CHECK_PAIR(v, code)						\
	do {								\
		if ((v).f0 != 2)					\
			return code;					\
		if ((v).f1 != -2)					\
			return (code) + 1;				\
	} while (0)

#define CHECK_SET_F0_FIRST(type, code)					\
	do {								\
		type v = {0, 0};					\
		v.f0 = 2;						\
		v.f1 = -2;						\
		CHECK_PAIR(v, code);					\
	} while (0)

#define CHECK_SET_F1_FIRST(type, code)					\
	do {								\
		type v = {0, 0};					\
		v.f1 = -2;						\
		v.f0 = 2;						\
		CHECK_PAIR(v, code);					\
	} while (0)

int
main(void)
{
	struct S_LONGLONG ll = {2, -2};
	struct S_CHAR_LONGLONG cll = {2, -2};
	struct S_SHORT_LONGLONG sll = {2, -2};
	struct S_INT_LONGLONG ill = {2, -2};

	CHECK_PAIR(g_ll, 1);
	CHECK_PAIR(g_cll, 3);
	CHECK_PAIR(g_sll, 5);
	CHECK_PAIR(g_ill, 7);

	CHECK_PAIR(ll, 9);
	CHECK_PAIR(cll, 11);
	CHECK_PAIR(sll, 13);
	CHECK_PAIR(ill, 15);

	CHECK_SET_F0_FIRST(struct S_LONGLONG, 17);
	CHECK_SET_F0_FIRST(struct S_CHAR_LONGLONG, 19);
	CHECK_SET_F0_FIRST(struct S_SHORT_LONGLONG, 21);
	CHECK_SET_F0_FIRST(struct S_INT_LONGLONG, 23);

	CHECK_SET_F1_FIRST(struct S_LONGLONG, 25);
	CHECK_SET_F1_FIRST(struct S_CHAR_LONGLONG, 27);
	CHECK_SET_F1_FIRST(struct S_SHORT_LONGLONG, 29);
	CHECK_SET_F1_FIRST(struct S_INT_LONGLONG, 31);

	return 0;
}
