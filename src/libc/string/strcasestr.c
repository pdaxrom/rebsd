#include <ctype.h>
#include <string.h>

char *
strcasestr(const char *haystack, const char *needle)
{
    const unsigned char *h, *n, *start;

    if (*needle == '\0')
        return (char *)haystack;
    for (; *haystack != '\0'; haystack++) {
        h = (const unsigned char *)haystack;
        n = (const unsigned char *)needle;
        start = h;
        while (*h != '\0' && *n != '\0' &&
            tolower(*h) == tolower(*n)) {
            h++;
            n++;
        }
        if (*n == '\0')
            return (char *)start;
    }
    return 0;
}
