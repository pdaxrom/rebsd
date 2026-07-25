#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int
vasprintf(char **result, const char *format, va_list ap)
{
    va_list copy;
    char *buffer;
    int length;

    *result = 0;
    va_copy(copy, ap);
    length = vsnprintf(0, 0, format, copy);
    va_end(copy);
    if (length < 0)
        return -1;
    buffer = malloc((size_t)length + 1);
    if (buffer == 0)
        return -1;
    va_copy(copy, ap);
    if (vsnprintf(buffer, (size_t)length + 1, format, copy) != length) {
        va_end(copy);
        free(buffer);
        return -1;
    }
    va_end(copy);
    *result = buffer;
    return length;
}

int
asprintf(char **result, const char *format, ...)
{
    va_list ap;
    int length;

    va_start(ap, format);
    length = vasprintf(result, format, ap);
    va_end(ap);
    return length;
}
