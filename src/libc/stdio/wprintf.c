#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

struct wout {
        FILE *fp;
        wchar_t *buf;
        size_t size;
        size_t len;
        int count;
        int err;
};

static void
append_dec(char **pp, int v)
{
        char tmp[16];
        char *p;
        unsigned int n;

        p = tmp + sizeof(tmp);
        *--p = 0;
        if (v < 0)
                n = (unsigned int)-v;
        else
                n = (unsigned int)v;
        do {
                *--p = (char)('0' + n % 10);
                n /= 10;
        } while (n);
        if (v < 0)
                *--p = '-';
        while (*p)
                *(*pp)++ = *p++;
}

static int
wout_putwc(struct wout *out, wchar_t wc)
{
        int c;

        if (out->fp) {
                c = wc <= 0xff ? wc : '?';
                if (putc(c, out->fp) == EOF) {
                        out->err = 1;
                        return EOF;
                }
        } else {
                if (out->len + 1 >= out->size) {
                        out->err = 1;
                } else {
                        out->buf[out->len] = wc;
                }
                out->len++;
        }
        out->count++;
        return (int)wc;
}

static void
wout_putpad(struct wout *out, int n)
{
        while (n-- > 0)
                wout_putwc(out, ' ');
}

static int
wout_putcstr(struct wout *out, const char *s, int width, int prec, int ladjust)
{
        int len, i;

        if (s == NULL)
                s = "(null)";
        for (len = 0; s[len] && (prec < 0 || len < prec); len++)
                ;
        if (!ladjust)
                wout_putpad(out, width - len);
        for (i = 0; i < len; i++)
                wout_putwc(out, (unsigned char)s[i]);
        if (ladjust)
                wout_putpad(out, width - len);
        return len;
}

static int
wout_putwstr(struct wout *out, const wchar_t *s, int width, int prec,
        int ladjust)
{
        static const wchar_t wnull[] = {
                '(', 'n', 'u', 'l', 'l', ')', 0
        };
        int len, i;

        if (s == NULL)
                s = wnull;
        for (len = 0; s[len] && (prec < 0 || len < prec); len++)
                ;
        if (!ladjust)
                wout_putpad(out, width - len);
        for (i = 0; i < len; i++)
                wout_putwc(out, s[i]);
        if (ladjust)
                wout_putpad(out, width - len);
        return len;
}

static void
build_spec(char *spec, const char *flags, int width, int prec, int length,
        int conv)
{
        char *p;
        const char *f;

        p = spec;
        *p++ = '%';
        for (f = flags; *f; f++)
                *p++ = *f;
        if (width >= 0)
                append_dec(&p, width);
        if (prec >= 0) {
                *p++ = '.';
                append_dec(&p, prec);
        }
        if (length == 'l') {
                *p++ = 'l';
        } else if (length == 2) {
                *p++ = 'l';
                *p++ = 'l';
        }
        *p++ = (char)conv;
        *p = 0;
}

static void
format_signed(struct wout *out, const char *spec, int length, va_list *ap)
{
        char tmp[256];

        if (length == 2)
                snprintf(tmp, sizeof(tmp), spec, va_arg(*ap, long long));
        else if (length == 'l')
                snprintf(tmp, sizeof(tmp), spec, va_arg(*ap, long));
        else
                snprintf(tmp, sizeof(tmp), spec, va_arg(*ap, int));
        wout_putcstr(out, tmp, -1, -1, 0);
}

static void
format_unsigned(struct wout *out, const char *spec, int length, va_list *ap)
{
        char tmp[256];

        if (length == 2)
                snprintf(tmp, sizeof(tmp), spec,
                    va_arg(*ap, unsigned long long));
        else if (length == 'l')
                snprintf(tmp, sizeof(tmp), spec, va_arg(*ap, unsigned long));
        else
                snprintf(tmp, sizeof(tmp), spec, va_arg(*ap, unsigned int));
        wout_putcstr(out, tmp, -1, -1, 0);
}

static void
format_float(struct wout *out, const char *spec, int length, va_list *ap)
{
        char tmp[256];
        double d;

        if (length == 'L')
                d = (double)va_arg(*ap, long double);
        else
                d = va_arg(*ap, double);
        snprintf(tmp, sizeof(tmp), spec, d);
        wout_putcstr(out, tmp, -1, -1, 0);
}

static int
has_flag(const char *flags, int flag)
{
        while (*flags)
                if (*flags++ == flag)
                        return 1;
        return 0;
}

static int
vwformat(struct wout *out, const wchar_t *fmt, va_list ap)
{
        char flags[8], spec[64];
        int c, width, prec, length, ladjust, nf;

        static const wchar_t nullfmt[] = {
                '(', 'n', 'u', 'l', 'l', ')', '\n', 0
        };
        wchar_t wcbuf[2];
        char cbuf[2];

        if (fmt == NULL)
                fmt = nullfmt;
        while ((c = *fmt++) != 0) {
                if (c != '%') {
                        wout_putwc(out, c);
                        continue;
                }
                if (*fmt == '%') {
                        fmt++;
                        wout_putwc(out, '%');
                        continue;
                }

                nf = 0;
                for (;;) {
                        c = *fmt;
                        if (c != '-' && c != '+' && c != ' ' &&
                            c != '#' && c != '0')
                                break;
                        if (nf < (int)sizeof(flags) - 1)
                                flags[nf++] = (char)c;
                        fmt++;
                }
                flags[nf] = 0;

                width = -1;
                if (*fmt == '*') {
                        width = va_arg(ap, int);
                        if (width < 0) {
                                width = -width;
                                if (!has_flag(flags, '-')) {
                                        flags[nf++] = '-';
                                        flags[nf] = 0;
                                }
                        }
                        fmt++;
                } else if (*fmt >= '0' && *fmt <= '9') {
                        width = 0;
                        while (*fmt >= '0' && *fmt <= '9') {
                                width = width * 10 + *fmt - '0';
                                fmt++;
                        }
                }

                prec = -1;
                if (*fmt == '.') {
                        fmt++;
                        if (*fmt == '*') {
                                prec = va_arg(ap, int);
                                if (prec < 0)
                                        prec = -1;
                                fmt++;
                        } else {
                                prec = 0;
                                while (*fmt >= '0' && *fmt <= '9') {
                                        prec = prec * 10 + *fmt - '0';
                                        fmt++;
                                }
                        }
                }

                length = 0;
                if (*fmt == 'h') {
                        fmt++;
                        if (*fmt == 'h')
                                fmt++;
                } else if (*fmt == 'l') {
                        fmt++;
                        if (*fmt == 'l') {
                                length = 2;
                                fmt++;
                        } else {
                                length = 'l';
                        }
                } else if (*fmt == 'L') {
                        length = 'L';
                        fmt++;
                }

                c = *fmt++;
                ladjust = has_flag(flags, '-');
                switch (c) {
                case 0:
                        return out->err ? EOF : out->count;
                case 'c':
                        if (length == 'l') {
                                wcbuf[0] = va_arg(ap, wint_t);
                                wcbuf[1] = 0;
                                wout_putwstr(out, wcbuf, width, 1, ladjust);
                        } else {
                                cbuf[0] = va_arg(ap, int);
                                cbuf[1] = 0;
                                wout_putcstr(out, cbuf, width, 1, ladjust);
                        }
                        break;
                case 's':
                        if (length == 'l')
                                wout_putwstr(out,
                                    va_arg(ap, const wchar_t *),
                                    width, prec, ladjust);
                        else
                                wout_putcstr(out,
                                    va_arg(ap, const char *),
                                    width, prec, ladjust);
                        break;
                case 'n':
                        if (length == 2)
                                *va_arg(ap, long long *) = out->count;
                        else if (length == 'l')
                                *va_arg(ap, long *) = out->count;
                        else
                                *va_arg(ap, int *) = out->count;
                        break;
                case 'd':
                case 'i':
                        build_spec(spec, flags, width, prec, length, c);
                        format_signed(out, spec, length, &ap);
                        break;
                case 'o':
                case 'u':
                case 'x':
                case 'X':
                case 'z':
                case 'Z':
                        build_spec(spec, flags, width, prec, length, c);
                        format_unsigned(out, spec, length, &ap);
                        break;
                case 'p': {
                        char tmp[256];
                        build_spec(spec, flags, width, prec, 0, c);
                        snprintf(tmp, sizeof(tmp), spec, va_arg(ap, void *));
                        wout_putcstr(out, tmp, -1, -1, 0);
                        break;
                }
                case 'b': {
                        char tmp[256];
                        build_spec(spec, flags, width, prec, 0, c);
                        snprintf(tmp, sizeof(tmp), spec, va_arg(ap, int),
                            va_arg(ap, char *));
                        wout_putcstr(out, tmp, -1, -1, 0);
                        break;
                }
                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                        build_spec(spec, flags, width, prec, 0, c);
                        format_float(out, spec, length, &ap);
                        break;
                default:
                        wout_putwc(out, '%');
                        if (length == 'l')
                                wout_putwc(out, 'l');
                        else if (length == 2) {
                                wout_putwc(out, 'l');
                                wout_putwc(out, 'l');
                        }
                        wout_putwc(out, c);
                        break;
                }
        }
        return out->err ? EOF : out->count;
}

int
fwide(FILE *fp, int mode)
{
        (void)fp;
        (void)mode;
        return 0;
}

wint_t
fputwc(wchar_t wc, FILE *fp)
{
        return putc(wc <= 0xff ? wc : '?', fp) == EOF ? WEOF : wc;
}

wint_t
putwc(wchar_t wc, FILE *fp)
{
        return fputwc(wc, fp);
}

wint_t
putwchar(wchar_t wc)
{
        return fputwc(wc, stdout);
}

int
fputws(const wchar_t *s, FILE *fp)
{
        while (*s)
                if (fputwc(*s++, fp) == WEOF)
                        return EOF;
        return ferror(fp) ? EOF : 0;
}

int
vfwprintf(FILE *fp, const wchar_t *fmt, va_list ap)
{
        struct wout out;

        out.fp = fp;
        out.buf = NULL;
        out.size = 0;
        out.len = 0;
        out.count = 0;
        out.err = 0;
        return vwformat(&out, fmt, ap);
}

int
fwprintf(FILE *fp, const wchar_t *fmt, ...)
{
        va_list ap;
        int rc;

        va_start(ap, fmt);
        rc = vfwprintf(fp, fmt, ap);
        va_end(ap);
        return rc;
}

int
vwprintf(const wchar_t *fmt, va_list ap)
{
        return vfwprintf(stdout, fmt, ap);
}

int
wprintf(const wchar_t *fmt, ...)
{
        va_list ap;
        int rc;

        va_start(ap, fmt);
        rc = vwprintf(fmt, ap);
        va_end(ap);
        return rc;
}

int
vswprintf(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap)
{
        struct wout out;
        int rc;

        out.fp = NULL;
        out.buf = buf;
        out.size = size;
        out.len = 0;
        out.count = 0;
        out.err = size == 0;
        rc = vwformat(&out, fmt, ap);
        if (size)
                buf[out.len < size ? out.len : size - 1] = 0;
        return rc;
}

int
swprintf(wchar_t *buf, size_t size, const wchar_t *fmt, ...)
{
        va_list ap;
        int rc;

        va_start(ap, fmt);
        rc = vswprintf(buf, size, fmt, ap);
        va_end(ap);
        return rc;
}
