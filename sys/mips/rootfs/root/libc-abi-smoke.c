#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <regex.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    char *dynamic;
    int anchor;
    int n;

    anchor = 5;
    if (vararg_values("mix", 17, 0x12345L, -0x112233445LL, 6.25,
        &anchor) != 0)
        return bad("vararg values");

    memset(buf, 0, sizeof(buf));
    format_into(buf, sizeof(buf), "%s:%d:%ld:%.1f", "fmt", 42,
        98765L, 3.5);
    if (strcmp(buf, "fmt:42:98765:3.5") != 0)
        return bad("vsnprintf varargs");
    memset(buf, 'X', sizeof(buf));
    n = snprintf(buf, 5, "%s:%d", "value", 27);
    if (n != 8 || strcmp(buf, "valu") != 0)
        return bad("snprintf required length");
    if (snprintf(NULL, 0, "%s:%d", "value", 27) != 8)
        return bad("snprintf zero size");
    dynamic = NULL;
    n = asprintf(&dynamic, "%s:%llu", "wide", 0x100000002ULL);
    if (n != 15 || dynamic == NULL ||
        strcmp(dynamic, "wide:4294967298") != 0) {
        free(dynamic);
        return bad("asprintf");
    }
    free(dynamic);
    return 0;
}

static int
check_parse(void)
{
    char *end;
    long sl;
    unsigned long ul;
    unsigned long long ull;
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
    ull = strtoull("0x100000002!", &end, 0);
    if (ull != 0x100000002ULL || *end != '!')
        return bad("strtoull");
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
    char *dynamic;
    char *field;
    char line[64];
    const char *checked;
    const char *fallback;
    size_t capacity, field_capacity;
    ssize_t length;
    FILE *fp;
    int fd;

    fd = mkstemp(path);
    if (fd < 0)
        return bad("mkstemp");
    if (dprintf(fd, "record:%d:%s\n", 37, "ok") != 13) {
        close(fd);
        unlink(path);
        return bad("dprintf");
    }
    fp = fdopen(fd, "r+");
    if (fp == NULL) {
        close(fd);
        unlink(path);
        return bad("fdopen");
    }
    if (fseeko(fp, 0, SEEK_SET) != 0 || ftello(fp) != 0) {
        fclose(fp);
        unlink(path);
        return bad("fseeko/ftello");
    }
    memset(line, 0, sizeof(line));
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        unlink(path);
        return bad("fgets");
    }
    if (fseek(fp, 0L, SEEK_SET) != 0 || ftell(fp) != 0L) {
        fclose(fp);
        unlink(path);
        return bad("fseek/ftell");
    }
    field = NULL;
    field_capacity = 0;
    length = getdelim(&field, &field_capacity, ':', fp);
    if (length != 7 || field == NULL || strcmp(field, "record:") != 0) {
        free(field);
        fclose(fp);
        unlink(path);
        return bad("getdelim");
    }
    free(field);
    if (fseeko(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        unlink(path);
        return bad("getline fseeko");
    }
    dynamic = NULL;
    capacity = 0;
    length = getline(&dynamic, &capacity, fp);
    if (length != 13 || dynamic == NULL ||
        strcmp(dynamic, "record:37:ok\n") != 0) {
        free(dynamic);
        fclose(fp);
        unlink(path);
        return bad("getline");
    }
    free(dynamic);
    fclose(fp);
    unlink(path);
    if (strcmp(line, "record:37:ok\n") != 0)
        return bad("stdio content");
    checked = "%s:%d";
    fallback = "%s:%ld";
    if (fmtcheck(checked, fallback) != checked ||
        fmtcheck("%s:%u", fallback) != fallback)
        return bad("fmtcheck");
    return 0;
}

static int
check_modern_types(void)
{
    char buffer[48];
    char *end;
    imaxdiv_t division;
    intmax_t signed_value;
    uintmax_t unsigned_value;
    size_t count;
    void *array;

    signed_value = strtoimax("-4294967298!", &end, 10);
    if (signed_value != -4294967298LL || *end != '!')
        return bad("strtoimax");
    unsigned_value = strtoumax("10000000002!", &end, 16);
    if (unsigned_value != 0x10000000002ULL || *end != '!')
        return bad("strtoumax");
    division = imaxdiv(-17, 5);
    if (division.quot != -3 || division.rem != -2 ||
        imaxabs(-1234567890123LL) != 1234567890123LL)
        return bad("intmax arithmetic");
    if (snprintf(buffer, sizeof(buffer), "%" PRIdMAX,
        (intmax_t)-4294967298LL) != 11 ||
        strcmp(buffer, "-4294967298") != 0)
        return bad("inttypes format");

    count = SIZE_MAX;
    errno = 0;
    array = reallocarray(NULL, count, 2);
    if (array != NULL || errno != ENOMEM) {
        free(array);
        return bad("reallocarray overflow");
    }
    array = reallocarray(NULL, 4, sizeof(int));
    if (array == NULL)
        return bad("reallocarray");
    free(array);
    return 0;
}

static int
check_directory(void)
{
    DIR *directory;
    struct dirent *entry;

    directory = opendir("/");
    if (directory == NULL)
        return bad("opendir");
    entry = readdir(directory);
    if (entry == NULL || entry->d_name[0] == '\0') {
        closedir(directory);
        return bad("readdir");
    }
    if (closedir(directory) != 0)
        return bad("closedir");
    return 0;
}

static int
check_modern_string(void)
{
    static const char haystack[] = "01234567";
    char buffer[8];
    char *copy;

    memset(buffer, 'X', sizeof(buffer));
    if (strlcpy(buffer, "abcdefghi", sizeof(buffer)) != 9 ||
        strcmp(buffer, "abcdefg") != 0)
        return bad("strlcpy");
    if (strlcat(buffer, "YZ", sizeof(buffer)) != 9 ||
        strcmp(buffer, "abcdefg") != 0)
        return bad("strlcat");
    copy = strndup("abcdef", 3);
    if (copy == NULL || strcmp(copy, "abc") != 0) {
        free(copy);
        return bad("strndup");
    }
    free(copy);
    if (strcasestr("Alpha-BETA", "ha-be") == NULL)
        return bad("strcasestr");
    if (memmem(haystack, 8, "345", 3) != (void *)(haystack + 3))
        return bad("memmem");
    return 0;
}

static int
check_regex(void)
{
    regex_t expression;
    regmatch_t match[3];
    int error;

    error = regcomp(&expression,
        "^([[:alpha:]]{2,4})-([0-9]{2})$",
        REG_EXTENDED | REG_ICASE);
    if (error != 0)
        return bad("regcomp ERE");
    error = regexec(&expression, "AbC-42", 3, match, 0);
    if (error != 0 || match[0].rm_so != 0 || match[0].rm_eo != 6 ||
        match[1].rm_so != 0 || match[1].rm_eo != 3 ||
        match[2].rm_so != 4 || match[2].rm_eo != 6) {
        regfree(&expression);
        return bad("regexec ERE/captures");
    }
    regfree(&expression);

    error = regcomp(&expression, "^two$", REG_EXTENDED | REG_NEWLINE);
    if (error != 0)
        return bad("regcomp newline");
    error = regexec(&expression, "one\ntwo\nthree", 1, match, 0);
    regfree(&expression);
    if (error != 0 || match[0].rm_so != 4 || match[0].rm_eo != 7)
        return bad("regexec newline");
    return 0;
}

static int
check_getopt(void)
{
    char *arguments[] = {
        "getopt-smoke", "operand", "-a", NULL
    };
    int option;

    opterr = 0;
    optind = 1;
    optreset = 1;
    option = getopt(3, arguments, "-a");
    if (option != 1 || optarg == NULL ||
        strcmp(optarg, "operand") != 0 || optind != 2)
        return bad("getopt in-order operand");
    if (getopt(3, arguments, "-a") != 'a' || optind != 3)
        return bad("getopt in-order option");
    if (getopt(3, arguments, "-a") != -1)
        return bad("getopt in-order end");
    return 0;
}

static int
check_getopt_long(void)
{
    static const struct option options[] = {
        { "color", required_argument, NULL, 256 },
        { "quiet", no_argument, NULL, 'q' },
        { NULL, 0, NULL, 0 }
    };
    char *arguments[] = {
        "getopt-smoke", "operand", "--color=always", "-q", NULL
    };
    int option;

    opterr = 0;
    optind = 0;
    optreset = 1;
    option = getopt_long(4, arguments, "q", options, NULL);
    if (option != 256 || optarg == NULL ||
        strcmp(optarg, "always") != 0)
        return bad("getopt_long argument");
    if (getopt_long(4, arguments, "q", options, NULL) != 'q')
        return bad("getopt_long short option");
    if (getopt_long(4, arguments, "q", options, NULL) != -1 ||
        optind != 3 || strcmp(arguments[optind], "operand") != 0)
        return bad("getopt_long permutation");
    return 0;
}

static int
check_time_locale(void)
{
    time_t clock;
    time_t local_clock;
    struct tm result;
    struct tm local_result;
    struct tm epoch;
    char text[26];

    if (setlocale(LC_ALL, "") == NULL ||
        strcmp(setlocale(LC_ALL, NULL), "C") != 0)
        return bad("C locale");
    clock = 0;
    if (gmtime_r(&clock, &result) != &result ||
        asctime_r(&result, text) != text || strlen(text) != 25)
        return bad("reentrant time");
    memset(&epoch, 0, sizeof(epoch));
    epoch.tm_year = 70;
    epoch.tm_mday = 1;
    if (timegm(&epoch) != 0 || epoch.tm_year != 70 ||
        epoch.tm_mon != 0 || epoch.tm_mday != 1)
        return bad("timegm");
    local_clock = 1234567;
    if (localtime_r(&local_clock, &local_result) != &local_result ||
        mktime(&local_result) != local_clock)
        return bad("mktime");
    if (difftime(10, 4) != 6.0)
        return bad("difftime");
    if (!isgreater(2.0, 1.0) || isless(2.0, 1.0) ||
        !isunordered(NAN, 1.0) || !isfinite(1.0) ||
        fpclassify(0.0) != FP_ZERO)
        return bad("C99 math classification");
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
    if (check_modern_types())
        return 1;
    if (check_directory())
        return 1;
    if (check_modern_string())
        return 1;
    if (check_regex())
        return 1;
    if (check_getopt())
        return 1;
    if (check_getopt_long())
        return 1;
    if (check_time_locale())
        return 1;
    printf("libc abi smoke ok\n");
    return 0;
}
