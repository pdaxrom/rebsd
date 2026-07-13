/* Check magic division and remainder against the hardware divide path. */

typedef unsigned int (*ufunc)(unsigned int);
typedef int (*sfunc)(int);

static volatile unsigned int udivisor;
static volatile int sdivisor;

#define UFUNCTIONS(tag, divisor) \
	unsigned int uquot_##tag(unsigned int value) { return value / divisor; } \
	unsigned int urem_##tag(unsigned int value) { return value % divisor; }

#define SFUNCTIONS(tag, divisor) \
	int squot_##tag(int value) { return value / divisor; } \
	int srem_##tag(int value) { return value % divisor; }

UFUNCTIONS(3, 3U)
UFUNCTIONS(7, 7U)
UFUNCTIONS(10, 10U)
UFUNCTIONS(31, 31U)
UFUNCTIONS(80000001, 0x80000001U)
UFUNCTIONS(fffffffd, 0xfffffffdU)

SFUNCTIONS(3, 3)
SFUNCTIONS(7, 7)
SFUNCTIONS(13, 13)
SFUNCTIONS(17, 17)
SFUNCTIONS(31, 31)
SFUNCTIONS(n3, -3)
SFUNCTIONS(n7, -7)
SFUNCTIONS(n13, -13)
SFUNCTIONS(n17, -17)
SFUNCTIONS(n31, -31)

struct ucase {
	unsigned int divisor;
	ufunc quot;
	ufunc rem;
};

struct scase {
	int divisor;
	sfunc quot;
	sfunc rem;
};

static struct ucase ucases[] = {
	{ 3U, uquot_3, urem_3 },
	{ 7U, uquot_7, urem_7 },
	{ 10U, uquot_10, urem_10 },
	{ 31U, uquot_31, urem_31 },
	{ 0x80000001U, uquot_80000001, urem_80000001 },
	{ 0xfffffffdU, uquot_fffffffd, urem_fffffffd }
};

static struct scase scases[] = {
	{ 3, squot_3, srem_3 }, { 7, squot_7, srem_7 },
	{ 13, squot_13, srem_13 }, { 17, squot_17, srem_17 },
	{ 31, squot_31, srem_31 }, { -3, squot_n3, srem_n3 },
	{ -7, squot_n7, srem_n7 }, { -13, squot_n13, srem_n13 },
	{ -17, squot_n17, srem_n17 }, { -31, squot_n31, srem_n31 }
};

static unsigned int uvalues[] = {
	0U, 1U, 2U, 3U, 6U, 7U, 9U, 10U, 30U, 31U, 32U,
	0x7ffffffeU, 0x7fffffffU, 0x80000000U, 0x80000001U,
	0xfffffffcU, 0xfffffffdU, 0xfffffffeU, 0xffffffffU
};

static int svalues[] = {
	(-2147483647 - 1), -2147483647, -1000000001, -32, -31, -30,
	-18, -17, -16, -14, -13, -12, -8, -7, -6, -4, -3, -2, -1,
	0, 1, 2, 3, 4, 6, 7, 8, 12, 13, 14, 16, 17, 18, 30, 31,
	32, 1000000001, 2147483646, 2147483647
};

static unsigned int
reference_uquot(unsigned int value)
{
	return value / udivisor;
}

static unsigned int
reference_urem(unsigned int value)
{
	return value % udivisor;
}

static int
reference_squot(int value)
{
	return value / sdivisor;
}

static int
reference_srem(int value)
{
	return value % sdivisor;
}

int
main(void)
{
	unsigned int i, j, state, value;
	int svalue;

	for (i = 0; i < sizeof(ucases) / sizeof(ucases[0]); i++) {
		udivisor = ucases[i].divisor;
		for (j = 0; j < sizeof(uvalues) / sizeof(uvalues[0]); j++) {
			value = uvalues[j];
			if (ucases[i].quot(value) != reference_uquot(value))
				return 1;
			if (ucases[i].rem(value) != reference_urem(value))
				return 2;
		}
		state = 0x9e3779b9U ^ ucases[i].divisor;
		for (j = 0; j < 1024; j++) {
			state = state * 1664525U + 1013904223U;
			if (ucases[i].quot(state) != reference_uquot(state))
				return 5;
			if (ucases[i].rem(state) != reference_urem(state))
				return 6;
		}
	}
	for (i = 0; i < sizeof(scases) / sizeof(scases[0]); i++) {
		sdivisor = scases[i].divisor;
		for (j = 0; j < sizeof(svalues) / sizeof(svalues[0]); j++) {
			svalue = svalues[j];
			if (scases[i].quot(svalue) != reference_squot(svalue))
				return 3;
			if (scases[i].rem(svalue) != reference_srem(svalue))
				return 4;
		}
		state = 0x7f4a7c15U ^ (unsigned int)scases[i].divisor;
		for (j = 0; j < 1024; j++) {
			state = state * 1664525U + 1013904223U;
			svalue = (int)state;
			if (scases[i].quot(svalue) != reference_squot(svalue))
				return 7;
			if (scases[i].rem(svalue) != reference_srem(svalue))
				return 8;
		}
	}
	return 0;
}
