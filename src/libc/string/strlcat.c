#include <string.h>

/*
 * Append src to dst of total size size and return the length of the string
 * that was requested, whether or not it fitted.
 */
size_t
strlcat(char *dst, const char *src, size_t size)
{
    size_t dlen, slen;

    dlen = 0;
    while (dlen < size && dst[dlen] != '\0')
        dlen++;
    slen = strlen(src);
    if (dlen == size)
        return size + slen;
    if (slen < size - dlen)
        memcpy(dst + dlen, src, slen + 1);
    else {
        memcpy(dst + dlen, src, size - dlen - 1);
        dst[size - 1] = '\0';
    }
    return dlen + slen;
}
