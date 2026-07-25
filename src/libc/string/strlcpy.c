#include <string.h>

/*
 * Copy src to dst of total size size.  Return the complete source length;
 * a return value greater than or equal to size therefore reports truncation.
 */
size_t
strlcpy(char *dst, const char *src, size_t size)
{
    const char *s;
    size_t left;

    s = src;
    left = size;
    if (left != 0) {
        while (--left != 0) {
            if ((*dst++ = *s++) == '\0')
                return (size_t)(s - src - 1);
        }
        *dst = '\0';
    }
    while (*s != '\0')
        s++;
    return (size_t)(s - src);
}
