#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static int
wascii(wchar_t wc)
{
        return wc >= 0 && wc <= 0x7f;
}

size_t
wcslen(const wchar_t *s)
{
        const wchar_t *p;

        for (p = s; *p; p++)
                ;
        return p - s;
}

size_t
wcsnlen(const wchar_t *s, size_t maxlen)
{
        size_t n;

        for (n = 0; n < maxlen && s[n]; n++)
                ;
        return n;
}

wchar_t *
wcscpy(wchar_t *dst, const wchar_t *src)
{
        wchar_t *ret;

        ret = dst;
        while ((*dst++ = *src++) != 0)
                ;
        return ret;
}

wchar_t *
wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
        wchar_t *ret;

        ret = dst;
        while (n && *src) {
                *dst++ = *src++;
                n--;
        }
        while (n) {
                *dst++ = 0;
                n--;
        }
        return ret;
}

wchar_t *
wcscat(wchar_t *dst, const wchar_t *src)
{
        wcscpy(dst + wcslen(dst), src);
        return dst;
}

wchar_t *
wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
        wchar_t *p;

        p = dst + wcslen(dst);
        while (n && *src) {
                *p++ = *src++;
                n--;
        }
        *p = 0;
        return dst;
}

int
wcscmp(const wchar_t *s1, const wchar_t *s2)
{
        while (*s1 == *s2) {
                if (*s1 == 0)
                        return 0;
                s1++;
                s2++;
        }
        return *s1 < *s2 ? -1 : 1;
}

int
wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
        while (n && *s1 == *s2) {
                if (*s1 == 0)
                        return 0;
                s1++;
                s2++;
                n--;
        }
        if (n == 0)
                return 0;
        return *s1 < *s2 ? -1 : 1;
}

wchar_t *
wcschr(const wchar_t *s, wchar_t c)
{
        for (;; s++) {
                if (*s == c)
                        return (wchar_t *)s;
                if (*s == 0)
                        return NULL;
        }
}

wchar_t *
wcsrchr(const wchar_t *s, wchar_t c)
{
        const wchar_t *last;

        last = NULL;
        for (;; s++) {
                if (*s == c)
                        last = s;
                if (*s == 0)
                        return (wchar_t *)last;
        }
}

wchar_t *
wcspbrk(const wchar_t *s, const wchar_t *charset)
{
        for (; *s; s++)
                if (wcschr(charset, *s))
                        return (wchar_t *)s;
        return NULL;
}

size_t
wcscspn(const wchar_t *s, const wchar_t *charset)
{
        const wchar_t *p;

        for (p = s; *p; p++)
                if (wcschr(charset, *p))
                        break;
        return p - s;
}

size_t
wcsspn(const wchar_t *s, const wchar_t *charset)
{
        const wchar_t *p;

        for (p = s; *p; p++)
                if (!wcschr(charset, *p))
                        break;
        return p - s;
}

wchar_t *
wcsstr(const wchar_t *s, const wchar_t *find)
{
        size_t len;

        if (*find == 0)
                return (wchar_t *)s;
        len = wcslen(find);
        for (; *s; s++)
                if (*s == *find && wcsncmp(s, find, len) == 0)
                        return (wchar_t *)s;
        return NULL;
}

wchar_t *
wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
        while (n--) {
                if (*s == c)
                        return (wchar_t *)s;
                s++;
        }
        return NULL;
}

int
wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
        while (n--) {
                if (*s1 != *s2)
                        return *s1 < *s2 ? -1 : 1;
                s1++;
                s2++;
        }
        return 0;
}

wchar_t *
wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
        wchar_t *ret;

        ret = dst;
        while (n--)
                *dst++ = *src++;
        return ret;
}

wchar_t *
wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
        size_t i;

        if (dst < src) {
                for (i = 0; i < n; i++)
                        dst[i] = src[i];
        } else if (dst > src) {
                while (n--)
                        dst[n] = src[n];
        }
        return dst;
}

wchar_t *
wmemset(wchar_t *s, wchar_t c, size_t n)
{
        wchar_t *ret;

        ret = s;
        while (n--)
                *s++ = c;
        return ret;
}

wint_t
btowc(int c)
{
        if (c == EOF)
                return WEOF;
        return (unsigned char)c <= 0x7f ? (unsigned char)c : WEOF;
}

int
wctob(wint_t wc)
{
        if (wc == WEOF || wc > 0x7f)
                return EOF;
        return (int)wc;
}

int
mbsinit(const mbstate_t *ps)
{
        return ps == NULL || ps->__count == 0;
}

size_t
mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
        unsigned char c;

        if (ps)
                ps->__count = 0;
        if (s == NULL)
                return 0;
        if (n == 0)
                return (size_t)-2;
        c = (unsigned char)*s;
        if (c == 0) {
                if (pwc)
                        *pwc = 0;
                return 0;
        }
        if (c > 0x7f)
                return (size_t)-1;
        if (pwc)
                *pwc = c;
        return 1;
}

size_t
mbrlen(const char *s, size_t n, mbstate_t *ps)
{
        return mbrtowc(NULL, s, n, ps);
}

size_t
wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
        if (ps)
                ps->__count = 0;
        if (s == NULL)
                return 1;
        if (!wascii(wc))
                return (size_t)-1;
        *s = (char)wc;
        return 1;
}

size_t
mbstowcs(wchar_t *dst, const char *src, size_t len)
{
        size_t n;
        unsigned char c;

        for (n = 0; n < len; n++) {
                c = (unsigned char)src[n];
                if (c > 0x7f)
                        return (size_t)-1;
                if (dst)
                        dst[n] = c;
                if (c == 0)
                        return n;
        }
        return n;
}

size_t
wcstombs(char *dst, const wchar_t *src, size_t len)
{
        size_t n;

        for (n = 0; n < len; n++) {
                if (!wascii(src[n]))
                        return (size_t)-1;
                if (dst)
                        dst[n] = (char)src[n];
                if (src[n] == 0)
                        return n;
        }
        return n;
}

size_t
mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
        size_t n;
        unsigned char c;
        const char *s;

        if (ps)
                ps->__count = 0;
        if (src == NULL || *src == NULL)
                return 0;
        s = *src;
        for (n = 0; n < len; n++, s++) {
                c = (unsigned char)*s;
                if (c > 0x7f)
                        return (size_t)-1;
                if (dst)
                        dst[n] = c;
                if (c == 0) {
                        *src = NULL;
                        return n;
                }
        }
        *src = s;
        return n;
}

size_t
wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps)
{
        size_t n;
        const wchar_t *s;

        if (ps)
                ps->__count = 0;
        if (src == NULL || *src == NULL)
                return 0;
        s = *src;
        for (n = 0; n < len; n++, s++) {
                if (!wascii(*s))
                        return (size_t)-1;
                if (dst)
                        dst[n] = (char)*s;
                if (*s == 0) {
                        *src = NULL;
                        return n;
                }
        }
        *src = s;
        return n;
}

static char *
wcs_to_ascii(const wchar_t *nptr, char **end)
{
        char *buf;
        size_t i, len;

        len = wcslen(nptr);
        buf = malloc(len + 1);
        if (buf == NULL) {
                if (end)
                        *end = NULL;
                return NULL;
        }
        for (i = 0; i < len; i++) {
                if (!wascii(nptr[i]))
                        break;
                buf[i] = (char)nptr[i];
        }
        buf[i] = 0;
        if (end)
                *end = buf + i;
        return buf;
}

static void
set_wend(const wchar_t *nptr, wchar_t **endptr, char *buf, char *end)
{
        if (endptr)
                *endptr = (wchar_t *)(nptr + (end ? end - buf : 0));
        free(buf);
}

long
wcstol(const wchar_t *nptr, wchar_t **endptr, int base)
{
        char *buf, *end;
        long v;

        buf = wcs_to_ascii(nptr, &end);
        if (buf == NULL) {
                if (endptr)
                        *endptr = (wchar_t *)nptr;
                return 0;
        }
        v = strtol(buf, &end, base);
        set_wend(nptr, endptr, buf, end);
        return v;
}

unsigned long
wcstoul(const wchar_t *nptr, wchar_t **endptr, int base)
{
        char *buf, *end;
        unsigned long v;

        buf = wcs_to_ascii(nptr, &end);
        if (buf == NULL) {
                if (endptr)
                        *endptr = (wchar_t *)nptr;
                return 0;
        }
        v = strtoul(buf, &end, base);
        set_wend(nptr, endptr, buf, end);
        return v;
}

double
wcstod(const wchar_t *nptr, wchar_t **endptr)
{
        char *buf, *end;
        double v;

        buf = wcs_to_ascii(nptr, &end);
        if (buf == NULL) {
                if (endptr)
                        *endptr = (wchar_t *)nptr;
                return 0.0;
        }
        v = strtod(buf, &end);
        set_wend(nptr, endptr, buf, end);
        return v;
}

float
wcstof(const wchar_t *nptr, wchar_t **endptr)
{
        return (float)wcstod(nptr, endptr);
}

long double
wcstold(const wchar_t *nptr, wchar_t **endptr)
{
        return (long double)wcstod(nptr, endptr);
}
