#include <locale.h>
#include <string.h>

static char c_locale[] = "C";
static char empty[] = "";

char *
setlocale(int category, const char *locale)
{
    if (category < LC_ALL || category > LC_TIME)
        return 0;
    if (locale == 0)
        return c_locale;
    if (*locale == '\0' || strcmp(locale, "C") == 0 ||
        strcmp(locale, "POSIX") == 0)
        return c_locale;
    return 0;
}

struct lconv *
localeconv(void)
{
    static struct lconv value = {
        ".", empty, empty, empty, empty, empty, empty, empty,
        empty, empty,
        127, 127, 127, 127, 127, 127, 127, 127
    };

    return &value;
}
