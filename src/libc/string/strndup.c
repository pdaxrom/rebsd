#include <stdlib.h>
#include <string.h>

char *
strndup(const char *src, size_t maxlen)
{
    char *copy;
    size_t len;

    len = 0;
    while (len < maxlen && src[len] != '\0')
        len++;
    copy = malloc(len + 1);
    if (copy == 0)
        return 0;
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}
