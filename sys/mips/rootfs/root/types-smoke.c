#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <netinet/in.h>

typedef signed char schar;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long llong;
typedef unsigned long long ullong;

extern int write();

enum small_enum {
	ENUM_NEG = -7,
	ENUM_POS = 23
};

struct layout_rec {
	char c;
	short s;
	int i;
	llong ll;
	float f;
	double d;
	char tail;
};

struct bits_rec {
	unsigned a:3;
	signed b:5;
	unsigned c:8;
};

union endian_word {
	ulong w;
	uchar b[4];
};

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SMOKE_LITTLE_ENDIAN 1
#elif defined(__MIPSEL__) || defined(__mipsel__) || defined(TARGET_LITTLE_ENDIAN)
#define SMOKE_LITTLE_ENDIAN 1
#else
#define SMOKE_LITTLE_ENDIAN 0
#endif

struct init_rec {
	int a;
	ulong u;
	llong ll;
	float f;
	double d;
	char text[4];
};

struct one_ulong_rec {
	ulong u;
};

struct byte_ulong_rec {
	char c;
	ulong u;
	char tail;
};

/* ReBSD uses the same 8-byte o32 aggregate alignment on every MIPS CPU. */
#define EXPECT_LAYOUT_DOUBLE_OFFSET 24
#define EXPECT_LAYOUT_TAIL_OFFSET 32
#define EXPECT_LAYOUT_SIZE 40

schar gsc = -5;
uchar guc = 250;
short gss = -1234;
ushort gus = 65000U;
int gi = -1234567;
uint gui = 0xfedcba98U;
long gl = -7654321L;
ulong gul = 0x89abcdefUL;
llong gll = -0x0000000100000002LL;
ullong gull = 0x8877665544332211ULL;
float gf = 1.25;
double gd = 2.5;
long double gld = 4.5;
enum small_enum ge = ENUM_POS;
char gtext[] = "type";
int gz;
char gzbuf[3];
static int sgz;
static llong sgllz;
int ginit_arr[5] = { 1, 2, 3 };
static short sginit_arr[4] = { -1, 2 };
struct init_rec ginit_rec =
    { 7, 0x12345678UL, 0x1122334455667788LL, 1.5, 2.5, "abc" };
static struct init_rec sginit_rec =
    { -7, 0x89abcdefUL, -0x0000000200000003LL, 3.5, 4.5, "xyz" };
char *gtext_ptr = gtext;
static char *sgtext_ptr = gtext;
const int gc_int = 314;
const ulong gc_ul = 0xcafebabeUL;
const llong gc_ll = 0x0102030405060708LL;
const float gc_float = 11.5;
const double gc_double = 12.5;
const char gc_text[] = "const";
const int *gc_int_ptr = &gc_int;
char * const gc_text_const_ptr = gtext;
static const int sgc_arr[4] = { 9, 8 };
static const struct init_rec sgc_rec =
    { 13, 0x10203040UL, 0x1020304050607080LL, 13.5, 14.5, "con" };

int
putstr(s)
	char *s;
{
	char *p;

	for (p = s; *p; p++)
		;
	write(1, s, p - s);
}

int
bad(s)
	char *s;
{
	putstr("types bad: ");
	putstr(s);
	putstr("\n");
	return 1;
}

int
close_float(v, e)
	float v;
	float e;
{
	float lo, hi;

	lo = e - (float)0.001;
	hi = e + (float)0.001;
	return v > lo && v < hi;
}

int
close_double(v, e)
	double v;
	double e;
{
	double lo, hi;

	lo = e - 0.000001;
	hi = e + 0.000001;
	return v > lo && v < hi;
}

int
close_ldouble(v, e)
	long double v;
	long double e;
{
	long double lo, hi;

	lo = e - (long double)0.000001;
	hi = e + (long double)0.000001;
	return v > lo && v < hi;
}

schar ret_schar() { return -9; }
uchar ret_uchar() { return 251; }
short ret_short() { return -2222; }
ushort ret_ushort() { return 60000U; }
int ret_int() { return -333333; }
uint ret_uint() { return 0xabcdef12U; }
long ret_long() { return -4444444L; }
ulong ret_ulong() { return 0x76543210UL; }
llong ret_llong() { return -0x0000000200000003LL; }
ullong ret_ullong() { return 0x0102030405060708ULL; }
enum small_enum ret_enum() { return ENUM_NEG; }

int
add_int(a, b)
	int a;
	int b;
{
	return a + b;
}

float
ret_float(a, b)
	float a;
	float b;
{
	return a * b + (float)0.5;
}

double
ret_double(a, b)
	double a;
	double b;
{
	return a / b + 1.0;
}

long double
ret_ldouble(a, b)
	long double a;
	long double b;
{
	return a + b;
}

int
check_scalar_args(a, b, c, d, e, f, g, h)
	schar a;
	uchar b;
	short c;
	ushort d;
	int e;
	uint f;
	long g;
	ulong h;
{
	return a == -5 && b == 250 && c == -1234 && d == 65000U &&
	    e == -1234567 && f == 0xfedcba98U &&
	    g == -7654321L && h == 0x89abcdefUL;
}

int
check_stack_scalar(a, b, c, d, e, f, g, h)
	int a;
	int b;
	int c;
	int d;
	schar e;
	uchar f;
	short g;
	ushort h;
{
	return a == 1 && b == 2 && c == 3 && d == 4 &&
	    e == -5 && f == 250 && g == -1234 && h == 65000U;
}

int
check_float_args(a, b, c, d, e)
	float a;
	float b;
	float c;
	float d;
	float e;
{
	return close_float(a + b + c + d + e, (float)15.0);
}

int
check_stack_float(a, b, c, d, e)
	int a;
	int b;
	int c;
	int d;
	float e;
{
	return a == 1 && b == 2 && c == 3 && d == 4 &&
	    close_float(e, (float)3.25);
}

int
check_stack_double2(a, b, c, d, e)
	int a;
	int b;
	int c;
	int d;
	double e;
{
	return a == 1 && b == 2 && c == 3 && d == 4 &&
	    close_double(e, 6.5);
}

int
check_sizes()
{
	struct rusage ru;
	struct shmid_ds shm;

	if (sizeof(char) != 1)
		return bad("sizeof char");
	if (sizeof(short) != 2)
		return bad("sizeof short");
	if (sizeof(int) != 4)
		return bad("sizeof int");
	if (sizeof(long) != 4)
		return bad("sizeof long");
	if (sizeof(char *) != 4)
		return bad("sizeof pointer");
	if (sizeof(llong) != 8)
		return bad("sizeof long long");
	if (sizeof(float) != 4)
		return bad("sizeof float");
	if (sizeof(double) != 8)
		return bad("sizeof double");
	if (sizeof(long double) != 8)
		return bad("sizeof long double");
	if (sizeof(enum small_enum) != 4)
		return bad("sizeof enum");
	if (sizeof(struct one_ulong_rec) != 4)
		return bad("sizeof struct ulong");
	if (sizeof(struct byte_ulong_rec) != 12)
		return bad("sizeof struct byte ulong");
	if (sizeof(struct in_addr) != 4)
		return bad("sizeof struct in_addr");
	if (sizeof(struct sockaddr_in) != 16)
		return bad("sizeof struct sockaddr_in");
	if (sizeof(struct timeval) != 16)
		return bad("sizeof struct timeval");
	if (sizeof(struct timespec) != 16)
		return bad("sizeof struct timespec");
	if (sizeof(struct itimerval) != 32)
		return bad("sizeof struct itimerval");
	if (sizeof(struct rusage) != 88)
		return bad("sizeof struct rusage");
	if ((int)((char *)&ru.ru_maxrss - (char *)&ru) != 32)
		return bad("struct rusage layout");
	if (sizeof(struct shmid_ds) != 72)
		return bad("sizeof struct shmid_ds");
	if ((int)((char *)&shm.shm_atime - (char *)&shm) != 48)
		return bad("struct shmid_ds layout");
	if (sizeof(struct stat) != 80)
		return bad("sizeof struct stat");
	return 0;
}

int
check_time_syscall_abi()
{
	volatile ulong before;
	struct timeval tv;
	volatile ulong after;

	before = 0x1234abcdUL;
	after = 0x89abcdefUL;
	if (gettimeofday(&tv, (struct timezone *)0) != 0)
		return bad("gettimeofday");
	if (before != 0x1234abcdUL || after != 0x89abcdefUL)
		return bad("gettimeofday stack overwrite");
	if (tv.tv_usec < 0 || tv.tv_usec >= 1000000L)
		return bad("gettimeofday usec");
	if (tv.tv_pad != 0)
		return bad("gettimeofday padding");
	return 0;
}

int
check_global_static_init()
{
	if (gz != 0 || sgz != 0 || sgllz != 0)
		return bad("bss zero init");
	if (gzbuf[0] != 0 || gzbuf[1] != 0 || gzbuf[2] != 0)
		return bad("bss array zero init");
	if (ginit_arr[0] != 1 || ginit_arr[1] != 2 ||
	    ginit_arr[2] != 3 || ginit_arr[3] != 0 || ginit_arr[4] != 0)
		return bad("global array init");
	if (sginit_arr[0] != -1 || sginit_arr[1] != 2 ||
	    sginit_arr[2] != 0 || sginit_arr[3] != 0)
		return bad("static array init");
	if (ginit_rec.a != 7 || ginit_rec.u != 0x12345678UL ||
	    ginit_rec.ll != 0x1122334455667788LL ||
	    ! close_float(ginit_rec.f, (float)1.5) ||
	    ! close_double(ginit_rec.d, 2.5) ||
	    ginit_rec.text[0] != 'a' || ginit_rec.text[1] != 'b' ||
	    ginit_rec.text[2] != 'c' || ginit_rec.text[3] != 0)
		return bad("global struct init");
	if (sginit_rec.a != -7 || sginit_rec.u != 0x89abcdefUL ||
	    sginit_rec.ll != -0x0000000200000003LL ||
	    ! close_float(sginit_rec.f, (float)3.5) ||
	    ! close_double(sginit_rec.d, 4.5) ||
	    sginit_rec.text[0] != 'x' || sginit_rec.text[1] != 'y' ||
	    sginit_rec.text[2] != 'z' || sginit_rec.text[3] != 0)
		return bad("static struct init");
	if (gtext_ptr != gtext || sgtext_ptr != gtext ||
	    gtext_ptr[0] != 't' || sgtext_ptr[3] != 'e')
		return bad("static pointer init");
	return 0;
}

int
check_local_init()
{
	int a = 5;
	int b[5] = { 1, 2, 3 };
	char s[] = "local";
	struct init_rec r =
	    { 9, 0x01020304UL, 0x0102030405060708LL, 5.5, 6.5, "lcl" };
	union endian_word u = { 0xaabbccddUL };

	if (a != 5)
		return bad("local scalar init");
	if (b[0] != 1 || b[1] != 2 || b[2] != 3 || b[3] != 0 || b[4] != 0)
		return bad("local array init");
	if (s[0] != 'l' || s[1] != 'o' || s[2] != 'c' ||
	    s[3] != 'a' || s[4] != 'l' || s[5] != 0)
		return bad("local string init");
	if (r.a != 9 || r.u != 0x01020304UL ||
	    r.ll != 0x0102030405060708LL ||
	    ! close_float(r.f, (float)5.5) ||
	    ! close_double(r.d, 6.5) ||
	    r.text[0] != 'l' || r.text[1] != 'c' ||
	    r.text[2] != 'l' || r.text[3] != 0)
		return bad("local struct init");
	if (SMOKE_LITTLE_ENDIAN) {
		if (u.b[0] != 0xdd || u.b[1] != 0xcc ||
		    u.b[2] != 0xbb || u.b[3] != 0xaa)
			return bad("local union init");
	} else {
		if (u.b[0] != 0xaa || u.b[1] != 0xbb ||
		    u.b[2] != 0xcc || u.b[3] != 0xdd)
			return bad("local union init");
	}
	return 0;
}

int
check_static_local_init(expect_call, expect_counter)
	int expect_call;
	int expect_counter;
{
	static int calls;
	static int zero;
	static int counter = 7;
	static llong sll = 0x1122334455667788LL;
	static float sf = 7.5;
	static double sd = 8.5;
	static int arr[4] = { 4, 5 };
	static struct init_rec r =
	    { 11, 0x55667788UL, 0x8877665544332211ULL, 9.5, 10.5, "stc" };

	calls++;
	if (calls != expect_call)
		return bad("static local call count");
	if (zero != 0)
		return bad("static local zero init");
	if (counter != expect_counter)
		return bad("static local scalar init");
	if (sll != 0x1122334455667788LL)
		return bad("static local long long init");
	if (! close_float(sf, (float)7.5) || ! close_double(sd, 8.5))
		return bad("static local fpu init");
	if (arr[0] != 4 || arr[1] != 5 || arr[2] != 0 || arr[3] != 0)
		return bad("static local array init");
	if (r.a != 11 || r.u != 0x55667788UL ||
	    r.ll != 0x8877665544332211ULL ||
	    ! close_float(r.f, (float)9.5) ||
	    ! close_double(r.d, 10.5) ||
	    r.text[0] != 's' || r.text[1] != 't' ||
	    r.text[2] != 'c' || r.text[3] != 0)
		return bad("static local struct init");
	counter++;
	return 0;
}

int
check_const_init()
{
	int li;
	ulong lu;
	float lf;
	double ld;
	char ls[12];
	int local;
	int *cp;

	li = -17;
	lu = 0x0badc0deUL;
	lf = 15.5;
	ld = 16.5;
	ls[0] = 'l';
	ls[1] = 'o';
	ls[2] = 'c';
	ls[3] = 'a';
	ls[4] = 'l';
	ls[5] = ' ';
	ls[6] = 'c';
	ls[7] = 'o';
	ls[8] = 'n';
	ls[9] = 's';
	ls[10] = 't';
	ls[11] = 0;
	local = 21;
	cp = &local;
	if (gc_int != 314)
		return bad("const global int");
	if (gc_ul != 0xcafebabeUL)
		return bad("const global ulong");
	if (gc_ll != 0x0102030405060708LL)
		return bad("const global long long");
	if (! close_float(gc_float, (float)11.5) ||
	    ! close_double(gc_double, 12.5))
		return bad("const global fpu");
	if (gc_text[0] != 'c' || gc_text[4] != 't' || gc_text[5] != 0)
		return bad("const global string");
	if (gc_int_ptr != &gc_int || *gc_int_ptr != 314)
		return bad("const pointer init");
	if (gc_text_const_ptr != gtext || gc_text_const_ptr[1] != 'y')
		return bad("const pointer-to-data init");
	if (sgc_arr[0] != 9 || sgc_arr[1] != 8 ||
	    sgc_arr[2] != 0 || sgc_arr[3] != 0)
		return bad("static const array init");
	if (sgc_rec.a != 13 || sgc_rec.u != 0x10203040UL ||
	    sgc_rec.ll != 0x1020304050607080LL ||
	    ! close_float(sgc_rec.f, (float)13.5) ||
	    ! close_double(sgc_rec.d, 14.5) ||
	    sgc_rec.text[0] != 'c' || sgc_rec.text[1] != 'o' ||
	    sgc_rec.text[2] != 'n' || sgc_rec.text[3] != 0)
		return bad("static const struct init");
	if (li != -17 || lu != 0x0badc0deUL ||
	    ! close_float(lf, (float)15.5) || ! close_double(ld, 16.5))
		return bad("local const scalar init");
	if (ls[0] != 'l' || ls[6] != 'c' || ls[10] != 't' || ls[11] != 0)
		return bad("local const string init");
	*cp = 22;
	if (local != 22)
		return bad("const pointer local");
	return 0;
}

int
check_integer_types()
{
	schar sc;
	uchar uc;
	short ss;
	ushort us;
	int i;
	uint ui;
	long l;
	ulong ul;

	sc = gsc;
	uc = guc;
	ss = gss;
	us = gus;
	i = gi;
	ui = gui;
	l = gl;
	ul = gul;

	if ((int)sc != -5 || (int)ret_schar() != -9)
		return bad("signed char");
	if ((int)uc != 250 || (int)ret_uchar() != 251)
		return bad("unsigned char");
	uc = uc + 10;
	if ((int)uc != 4)
		return bad("unsigned char wrap");
	if ((int)ss != -1234 || (int)ret_short() != -2222)
		return bad("short");
	if ((uint)us != 65000U || (uint)ret_ushort() != 60000U)
		return bad("unsigned short");
	us = us + 1000U;
	if ((uint)us != 464U)
		return bad("unsigned short wrap");
	if (i != -1234567 || ret_int() != -333333)
		return bad("int");
	if (ui != 0xfedcba98U || ret_uint() != 0xabcdef12U)
		return bad("unsigned int");
	if (l != -7654321L || ret_long() != -4444444L)
		return bad("long");
	if (ul != 0x89abcdefUL || ret_ulong() != 0x76543210UL)
		return bad("unsigned long");
	if (((ulong)gll != 0xfffffffeUL) || ((long)(gll >> 32) != -2L))
		return bad("long long global");
	if (ret_llong() != -0x0000000200000003LL)
		return bad("long long return");
	if (gull != 0x8877665544332211ULL ||
	    ret_ullong() != 0x0102030405060708ULL)
		return bad("unsigned long long");
	if (ge != ENUM_POS || ret_enum() != ENUM_NEG)
		return bad("enum");
	if (! check_scalar_args(gsc, guc, gss, gus, gi, gui, gl, gul))
		return bad("scalar args");
	if (! check_stack_scalar(1, 2, 3, 4, gsc, guc, gss, gus))
		return bad("stack scalar args");
	return 0;
}

int
check_pointer_types()
{
	int a[4];
	int *p;
	char *s;
	int (*fp)();

	a[0] = 11;
	a[1] = 22;
	a[2] = 33;
	a[3] = 44;
	p = a;
	if (*(p + 2) != 33)
		return bad("pointer arithmetic");
	p++;
	if (p[-1] != 11 || p[2] != 44)
		return bad("pointer index");
	s = gtext;
	if (s[0] != 't' || *(s + 3) != 'e')
		return bad("char pointer");
	fp = add_int;
	if ((*fp)(7, 5) != 12)
		return bad("function pointer");
	return 0;
}

int
check_float_types()
{
	float f;
	double d;
	long double ld;
	int i;

	f = gf;
	f = (f + (float)2.0) * (float)4.0 / (float)2.0;
	if (! close_float(f, (float)6.5))
		return bad("float arithmetic");
	if (! close_float(ret_float((float)1.5, (float)2.0), (float)3.5))
		return bad("float return");
	if (! check_float_args((float)1.0, (float)2.0, (float)3.0,
	    (float)4.0, (float)5.0))
		return bad("float args");
	if (! check_stack_float(1, 2, 3, 4, (float)3.25))
		return bad("stack float arg");

	d = gd;
	d = d * 4.0 - 1.0;
	if (! close_double(d, 9.0))
		return bad("double arithmetic");
	if (! close_double(ret_double(9.0, 3.0), 4.0))
		return bad("double return");
	if (! check_stack_double2(1, 2, 3, 4, 6.5))
		return bad("stack double arg");

	ld = gld;
	ld = ret_ldouble(ld, (long double)1.5);
	if (! close_ldouble(ld, (long double)6.0))
		return bad("long double");

	i = (int)(float)-3.75;
	if (i != -3)
		return bad("float to int trunc");
	i = (int)3.75;
	if (i != 3)
		return bad("double to int trunc");
	f = (float)7;
	if (! close_float(f, (float)7.0))
		return bad("int to float");
	d = (double)-9;
	if (! close_double(d, -9.0))
		return bad("int to double");
	return 0;
}

int
check_aggregate_types()
{
	struct layout_rec r;
	struct bits_rec bits;
	union endian_word u;

	if ((int)((char *)&r.s - (char *)&r) != 2)
		return bad("struct short offset");
	if ((int)((char *)&r.i - (char *)&r) != 4)
		return bad("struct int offset");
	if ((int)((char *)&r.ll - (char *)&r) != 8)
		return bad("struct long long offset");
	if ((int)((char *)&r.f - (char *)&r) != 16)
		return bad("struct float offset");
	if ((int)((char *)&r.d - (char *)&r) != EXPECT_LAYOUT_DOUBLE_OFFSET)
		return bad("struct double offset");
	if ((int)((char *)&r.tail - (char *)&r) != EXPECT_LAYOUT_TAIL_OFFSET)
		return bad("struct tail offset");
	if (sizeof(struct layout_rec) != EXPECT_LAYOUT_SIZE)
		return bad("struct size");

	r.c = -1;
	r.s = -2;
	r.i = -3;
	r.ll = 0x1122334455667788LL;
	r.f = (float)1.5;
	r.d = 2.5;
	r.tail = 7;
	if (r.c != -1 || r.s != -2 || r.i != -3 ||
	    r.ll != 0x1122334455667788LL ||
	    ! close_float(r.f, (float)1.5) ||
	    ! close_double(r.d, 2.5) || r.tail != 7)
		return bad("struct values");

	bits.a = 5;
	bits.b = -3;
	bits.c = 0xaa;
	if (bits.a != 5 || bits.b != -3 || bits.c != 0xaa)
		return bad("bitfields");

	u.w = 0x11223344UL;
	if (SMOKE_LITTLE_ENDIAN) {
		if (u.b[0] != 0x44 || u.b[1] != 0x33 ||
		    u.b[2] != 0x22 || u.b[3] != 0x11)
			return bad("endian union");
	} else {
		if (u.b[0] != 0x11 || u.b[1] != 0x22 ||
		    u.b[2] != 0x33 || u.b[3] != 0x44)
			return bad("endian union");
	}
	return 0;
}

int
main()
{
	if (check_sizes())
		return 1;
	if (check_time_syscall_abi())
		return 1;
	if (check_global_static_init())
		return 1;
	if (check_local_init())
		return 1;
	if (check_static_local_init(1, 7))
		return 1;
	if (check_static_local_init(2, 8))
		return 1;
	if (check_const_init())
		return 1;
	if (check_integer_types())
		return 1;
	if (check_pointer_types())
		return 1;
	if (check_float_types())
		return 1;
	if (check_aggregate_types())
		return 1;
	putstr("types smoke ok\n");
	return 0;
}
