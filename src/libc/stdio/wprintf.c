#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

static int
wputstr(const wchar_t *s)
{
        int n;

        if (s == NULL)
                s = L"(null)";
        for (n = 0; *s; s++, n++)
                putchar((unsigned char)*s);
        return n;
}

int
wprintf(const wchar_t *fmt, ...)
{
        va_list ap;
        int count;

        va_start(ap, fmt);
        count = 0;
        while (*fmt) {
                if (*fmt != L'%') {
                        putchar((unsigned char)*fmt++);
                        count++;
                        continue;
                }
                fmt++;
                if (*fmt == L'%') {
                        putchar('%');
                        fmt++;
                        count++;
                        continue;
                }
                if (*fmt == L'l' && fmt[1] == L's') {
                        count += wputstr(va_arg(ap, const wchar_t *));
                        fmt += 2;
                        continue;
                }
                if (*fmt == L'l' && fmt[1] == L'c') {
                        putchar((unsigned char)va_arg(ap, wint_t));
                        fmt += 2;
                        count++;
                        continue;
                }
                putchar('%');
                count++;
        }
        va_end(ap);
        return ferror(stdout) ? EOF : count;
}
