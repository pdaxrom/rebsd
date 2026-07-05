#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct smoke_record {
    int key;
    long serial;
    double weight;
    char name[8];
    union {
        long long ll;
        unsigned char bytes[8];
    } payload;
};

static jmp_buf jump_env;

static int
bad(const char *tag)
{
    printf("libc-abi-smoke fail: %s\n", tag);
    return 1;
}

static struct smoke_record
make_record(int key, long serial, double weight, const char *name, long long ll)
{
    struct smoke_record r;

    r.key = key;
    r.serial = serial;
    r.weight = weight;
    memset(r.name, 0, sizeof(r.name));
    strncpy(r.name, name, sizeof(r.name) - 1);
    r.payload.ll = ll;
    return r;
}

static int
consume_record(struct smoke_record r, int expect_key, const char *expect_name)
{
    if (r.key != expect_key)
        return 1;
    if (strcmp(r.name, expect_name) != 0)
        return 2;
    if (r.serial != 1234567L)
        return 3;
    if (r.weight < 12.4 || r.weight > 12.6)
        return 4;
    if (r.payload.ll != 0x1122334455667788LL)
        return 5;
    return 0;
}

static int
check_records(void)
{
    struct smoke_record r;
    struct smoke_record a[4];
    struct smoke_record *p;
    ptrdiff_t d;
    int rc;

    r = make_record(7, 1234567L, 12.5, "alpha",
        0x1122334455667788LL);
    rc = consume_record(r, 7, "alpha");
    if (rc != 0) {
        printf("libc-abi-smoke detail: consume_record rc=%d\n", rc);
        return bad("struct return/by-value");
    }
    if (offsetof(struct smoke_record, payload) <=
        offsetof(struct smoke_record, name))
        return bad("struct layout");

    a[0] = make_record(4, 10, 1.0, "d", 4);
    a[1] = make_record(1, 11, 2.0, "a", 1);
    a[2] = make_record(3, 12, 3.0, "c", 3);
    a[3] = make_record(2, 13, 4.0, "b", 2);

    p = a;
    if ((p + 3)->key != 2)
        return bad("pointer index");
    d = (p + 3) - p;
    if (d != 3)
        return bad("pointer difference");
    if ((char *)&a[1] - (char *)&a[0] != (ptrdiff_t)sizeof(a[0]))
        return bad("byte pointer difference");
    return 0;
}

static int
record_cmp(const void *av, const void *bv)
{
    const struct smoke_record *a;
    const struct smoke_record *b;

    a = av;
    b = bv;
    return a->key - b->key;
}

static int
check_qsort(void)
{
    struct smoke_record a[5];
    int i;

    a[0] = make_record(41, 1, 1.0, "e", 1);
    a[1] = make_record(7, 2, 2.0, "b", 2);
    a[2] = make_record(19, 3, 3.0, "c", 3);
    a[3] = make_record(3, 4, 4.0, "a", 4);
    a[4] = make_record(23, 5, 5.0, "d", 5);

    qsort(a, 5, sizeof(a[0]), record_cmp);
    for (i = 1; i != 5; ++i) {
        if (a[i - 1].key > a[i].key)
            return bad("qsort order");
    }
    if (strcmp(a[0].name, "a") != 0 || strcmp(a[4].name, "e") != 0)
        return bad("qsort payload");
    return 0;
}

static int
vararg_values(const char *tag, ...)
{
    va_list ap;
    int i;
    long l;
    long long ll;
    double d;
    void *p;

    va_start(ap, tag);
    i = va_arg(ap, int);
    l = va_arg(ap, long);
    ll = va_arg(ap, long long);
    d = va_arg(ap, double);
    p = va_arg(ap, void *);
    va_end(ap);

    if (strcmp(tag, "mix") != 0)
        return 1;
    if (i != 17 || l != 0x12345L)
        return 2;
    if (ll != -0x112233445LL)
        return 3;
    if (d < 6.24 || d > 6.26)
        return 4;
    if (p == NULL)
        return 5;
    return 0;
}

static void
format_into(char *buf, size_t len, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, len, fmt, ap);
    va_end(ap);
}

static int
check_varargs(void)
{
    char buf[64];
    int anchor;

    anchor = 5;
    if (vararg_values("mix", 17, 0x12345L, -0x112233445LL, 6.25,
        &anchor) != 0)
        return bad("vararg values");

    memset(buf, 0, sizeof(buf));
    format_into(buf, sizeof(buf), "%s:%d:%ld:%.1f", "fmt", 42,
        98765L, 3.5);
    if (strcmp(buf, "fmt:42:98765:3.5") != 0)
        return bad("vsnprintf varargs");
    return 0;
}

static int
check_parse(void)
{
    char *end;
    long sl;
    unsigned long ul;
    double d;
    int i;
    long l;
    char word[8];

    sl = strtol(" -1234x", &end, 0);
    if (sl != -1234 || *end != 'x')
        return bad("strtol");
    ul = strtoul("0xff!", &end, 0);
    if (ul != 255UL || *end != '!')
        return bad("strtoul");
    d = strtod("12.75;", &end);
    if (d < 12.74 || d > 12.76 || *end != ';')
        return bad("strtod");

    memset(word, 0, sizeof(word));
    if (sscanf("17 12345 abc", "%d %ld %7s", &i, &l, word) != 3)
        return bad("sscanf count");
    if (i != 17 || l != 12345L || strcmp(word, "abc") != 0)
        return bad("sscanf values");
    return 0;
}

static void
jump_out(int value)
{
    longjmp(jump_env, value);
}

static int
check_setjmp(void)
{
	int rc;

#if defined(__mips__)
# if defined(__mips_hard_float) && defined(__mips_soft_float)
	return bad("mips float macro conflict");
# endif
# if !defined(__mips_hard_float) && !defined(__mips_soft_float)
	return bad("mips float macro missing");
# endif
# if defined(__mips_hard_float)
	if (sizeof(jump_env) != 47 * sizeof(int))
		return bad("jmp_buf hard-float size");
# else
	if (sizeof(jump_env) != 14 * sizeof(int))
		return bad("jmp_buf soft-float size");
# endif
#endif

	rc = setjmp(jump_env);
	if (rc == 0)
		jump_out(23);
    if (rc != 23)
        return bad("setjmp value");
    return 0;
}

static int
check_stdio_file(void)
{
    char path[] = "/var/tmp/libc-abi-smoke.XXXXXX";
    char line[64];
    FILE *fp;
    int fd;

    fd = mkstemp(path);
    if (fd < 0)
        return bad("mkstemp");
    fp = fdopen(fd, "w+");
    if (fp == NULL) {
        close(fd);
        unlink(path);
        return bad("fdopen");
    }
    fprintf(fp, "record:%d:%s\n", 37, "ok");
    fflush(fp);
    if (fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        unlink(path);
        return bad("fseek");
    }
    memset(line, 0, sizeof(line));
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        unlink(path);
        return bad("fgets");
    }
    fclose(fp);
    unlink(path);
    if (strcmp(line, "record:37:ok\n") != 0)
        return bad("stdio content");
    return 0;
}

int
main(void)
{
    if (check_records())
        return 1;
    if (check_qsort())
        return 1;
    if (check_varargs())
        return 1;
    if (check_parse())
        return 1;
    if (check_setjmp())
        return 1;
    if (check_stdio_file())
        return 1;
    printf("libc abi smoke ok\n");
    return 0;
}
