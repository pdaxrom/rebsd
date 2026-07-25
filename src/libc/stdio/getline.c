#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t
getdelim(char **line, size_t *capacity, int delimiter, FILE *stream)
{
    char *buffer;
    size_t used;

    if (line == 0 || capacity == 0 || stream == 0) {
        errno = EINVAL;
        return -1;
    }
    buffer = *line;
    if (buffer == 0 || *capacity == 0) {
        *capacity = BUFSIZ;
        buffer = malloc(*capacity);
        if (buffer == 0)
            return -1;
        *line = buffer;
    }
    used = 0;
    for (;;) {
        int ch;

        ch = fgetc(stream);
        if (ch == EOF) {
            if (used == 0)
                return -1;
            break;
        }
        if (used + 1 >= *capacity) {
            size_t new_capacity;
            char *new_buffer;

            if (*capacity > (size_t)-1 / 2) {
                errno = EOVERFLOW;
                return -1;
            }
            new_capacity = *capacity * 2;
            new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == 0)
                return -1;
            buffer = new_buffer;
            *line = buffer;
            *capacity = new_capacity;
        }
        buffer[used++] = (char)ch;
        if (ch == delimiter)
            break;
    }
    buffer[used] = '\0';
    if (used > 0x7fffffffU) {
        errno = EOVERFLOW;
        return -1;
    }
    return (ssize_t)used;
}

ssize_t
getline(char **line, size_t *capacity, FILE *stream)
{
    return getdelim(line, capacity, '\n', stream);
}
