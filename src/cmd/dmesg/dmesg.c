/*
 * Display the kernel message buffer through the BSD kern.msgbuf sysctl.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int
write_all(int fd, const char *buffer, size_t length)
{
    ssize_t count;

    while (length != 0) {
        count = write(fd, buffer, length);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0) {
            errno = EIO;
            return -1;
        }
        buffer += count;
        length -= count;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    int mib[2] = { CTL_KERN, KERN_MSGBUF };
    char *buffer;
    size_t capacity;
    size_t length;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }

    capacity = 0;
    if (sysctl(mib, 2, NULL, &capacity, NULL, 0) < 0) {
        perror("dmesg: kern.msgbuf");
        return 1;
    }
    buffer = malloc(capacity == 0 ? 1 : capacity);
    if (buffer == NULL) {
        perror("dmesg: malloc");
        return 1;
    }

    length = capacity;
    if (sysctl(mib, 2, buffer, &length, NULL, 0) < 0) {
        perror("dmesg: kern.msgbuf");
        free(buffer);
        return 1;
    }
    if (write_all(STDOUT_FILENO, buffer, length) < 0) {
        perror("dmesg: stdout");
        free(buffer);
        return 1;
    }
    if (length != 0 && buffer[length - 1] != '\n' &&
        write_all(STDOUT_FILENO, "\n", 1) < 0) {
        perror("dmesg: stdout");
        free(buffer);
        return 1;
    }
    free(buffer);
    return 0;
}
