#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
vdprintf(int fd, const char *format, va_list ap)
{
    char *buffer;
    size_t done;
    int length;

    length = vasprintf(&buffer, format, ap);
    if (length < 0)
        return -1;
    done = 0;
    while (done < (size_t)length) {
        ssize_t n;

        n = write(fd, buffer + done, (size_t)length - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            free(buffer);
            return -1;
        }
        done += (size_t)n;
    }
    free(buffer);
    return length;
}

int
dprintf(int fd, const char *format, ...)
{
    va_list ap;
    int length;

    va_start(ap, format);
    length = vdprintf(fd, format, ap);
    va_end(ap);
    return length;
}
