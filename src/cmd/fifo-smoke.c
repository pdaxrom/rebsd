/*
 * Regression coverage for POSIX named FIFO semantics shared by every port.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FIFO_PATH       "/var/fifo-smoke"
#define ATOMIC_WRITES   8

static int failures;

static void
check(int condition, const char *what)
{
    if (condition)
        return;
    fprintf(stderr, "fifo-smoke: %s failed (errno=%d)\n", what, errno);
    failures++;
}

static int
make_fifo(void)
{
    (void)unlink(FIFO_PATH);
    if (mkfifo(FIFO_PATH, 0600) < 0) {
        fprintf(stderr, "fifo-smoke: mkfifo failed (errno=%d)\n", errno);
        failures++;
        return -1;
    }
    return 0;
}

static int
read_full(int fd, char *buffer, int length)
{
    int done, count;

    done = 0;
    while (done != length) {
        count = read(fd, buffer + done, length - done);
        if (count <= 0)
            return -1;
        done += count;
    }
    return 0;
}

static void
test_basic(void)
{
    struct stat sb;
    struct timeval zero;
    fd_set reads;
    long queued;
    char data[4];
    int reader, writer, result;

    if (make_fifo() < 0)
        return;
    check(stat(FIFO_PATH, &sb) == 0 && S_ISFIFO(sb.st_mode), "stat type");

    errno = 0;
    writer = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    check(writer < 0 && errno == ENXIO, "writer without reader");
    if (writer >= 0)
        close(writer);

    reader = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    check(reader >= 0, "nonblocking reader open");
    if (reader < 0)
        goto out;
    check(read(reader, data, sizeof(data)) == 0, "EOF without writer");

    writer = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    check(writer >= 0, "writer with reader");
    if (writer < 0) {
        close(reader);
        goto out;
    }
    errno = 0;
    check(read(reader, data, sizeof(data)) < 0 && errno == EWOULDBLOCK,
        "empty nonblocking read");
    check(write(writer, "abc", 3) == 3, "write");

    queued = -1;
    check(ioctl(reader, FIONREAD, &queued) == 0 && queued == 3,
        "FIONREAD");
    check(fstat(reader, &sb) == 0 && sb.st_size == 3, "fstat queued size");
    FD_ZERO(&reads);
    FD_SET(reader, &reads);
    zero.tv_sec = 0;
    zero.tv_usec = 0;
    result = select(reader + 1, &reads, NULL, NULL, &zero);
    check(result == 1 && FD_ISSET(reader, &reads), "select readable");
    check(read(reader, data, 2) == 2 && data[0] == 'a' && data[1] == 'b',
        "partial read");
    check(read(reader, data, sizeof(data)) == 1 && data[0] == 'c',
        "remaining read");

    errno = 0;
    check(lseek(reader, 0, SEEK_SET) < 0 && errno == ESPIPE, "lseek");
    errno = 0;
    check(fsync(writer) < 0 && errno == EINVAL, "fsync");
    close(writer);
    check(read(reader, data, sizeof(data)) == 0, "EOF after writer close");
    close(reader);
out:
    (void)unlink(FIFO_PATH);
}

static void
test_unlink_and_sigpipe(void)
{
    struct stat sb;
    char byte;
    int reader, writer;

    if (make_fifo() < 0)
        return;
    reader = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    writer = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    check(reader >= 0 && writer >= 0, "unlink setup");
    if (reader < 0 || writer < 0)
        goto out;
    check(unlink(FIFO_PATH) == 0, "unlink open FIFO");
    errno = 0;
    check(stat(FIFO_PATH, &sb) < 0 && errno == ENOENT, "unlinked name");
    check(write(writer, "u", 1) == 1 && read(reader, &byte, 1) == 1 &&
        byte == 'u', "I/O after unlink");
    close(reader);
    reader = -1;
    (void)signal(SIGPIPE, SIG_IGN);
    errno = 0;
    check(write(writer, "x", 1) < 0 && errno == EPIPE, "SIGPIPE/EPIPE");
out:
    if (reader >= 0)
        close(reader);
    if (writer >= 0)
        close(writer);
    (void)unlink(FIFO_PATH);
}

static void
test_blocking_open(void)
{
    int status, writer;
    pid_t child;

    if (make_fifo() < 0)
        return;
    child = fork();
    check(child >= 0, "blocking fork");
    if (child == 0) {
        char byte;
        int reader;

        reader = open(FIFO_PATH, O_RDONLY);
        if (reader < 0 || read(reader, &byte, 1) != 1 || byte != 'z')
            _exit(1);
        close(reader);
        _exit(0);
    }
    if (child < 0)
        goto out;
    sleep(1);
    writer = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    check(writer >= 0, "blocked reader is present");
    if (writer >= 0) {
        check(write(writer, "z", 1) == 1, "wake blocked reader");
        close(writer);
    }
    status = -1;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0, "blocking transfer");
out:
    (void)unlink(FIFO_PATH);
}

static void
atomic_writer(char marker)
{
    char record[PIPE_BUF];
    int fd, i;

    memset(record, marker, sizeof(record));
    fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0)
        _exit(1);
    for (i = 0; i < ATOMIC_WRITES; i++) {
        if (write(fd, record, sizeof(record)) != sizeof(record))
            _exit(1);
    }
    close(fd);
    _exit(0);
}

static void
test_multiple_and_atomic(void)
{
    char record[PIPE_BUF], bytes[2];
    int anchor, r1, r2, writer, status, i, j, acount, bcount;
    pid_t a, b;

    if (make_fifo() < 0)
        return;
    r1 = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    r2 = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    writer = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    check(r1 >= 0 && r2 >= 0 && writer >= 0, "multiple endpoints");
    if (r1 >= 0 && r2 >= 0 && writer >= 0) {
        check(write(writer, "xy", 2) == 2, "multiple reader write");
        check(read(r1, &bytes[0], 1) == 1 &&
            read(r2, &bytes[1], 1) == 1 &&
            bytes[0] == 'x' && bytes[1] == 'y', "multiple readers");
    }
    if (writer >= 0)
        close(writer);
    if (r2 >= 0)
        close(r2);
    if (r1 >= 0)
        close(r1);

    anchor = open(FIFO_PATH, O_RDWR);
    check(anchor >= 0, "O_RDWR anchor");
    if (anchor < 0)
        goto out;
    a = fork();
    if (a == 0)
        atomic_writer('A');
    b = fork();
    if (b == 0)
        atomic_writer('B');
    check(a >= 0 && b >= 0, "atomic writer forks");
    acount = 0;
    bcount = 0;
    if (a >= 0 && b >= 0) {
        for (i = 0; i < ATOMIC_WRITES * 2; i++) {
            if (read_full(anchor, record, sizeof(record)) < 0) {
                check(0, "atomic record read");
                break;
            }
            for (j = 1; j < (int)sizeof(record); j++) {
                if (record[j] != record[0]) {
                    check(0, "PIPE_BUF atomicity");
                    break;
                }
            }
            if (record[0] == 'A')
                acount++;
            else if (record[0] == 'B')
                bcount++;
            else
                check(0, "atomic record marker");
        }
        status = -1;
        check(waitpid(a, &status, 0) == a && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0, "writer A status");
        status = -1;
        check(waitpid(b, &status, 0) == b && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0, "writer B status");
        check(acount == ATOMIC_WRITES && bcount == ATOMIC_WRITES,
            "atomic record counts");
    }
    close(anchor);
out:
    (void)unlink(FIFO_PATH);
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: fifo-smoke [basic|unlink|blocking|atomic]\n");
}

int
main(int argc, char **argv)
{
    if (argc > 2) {
        usage();
        return 2;
    }
    if (argc == 1 || strcmp(argv[1], "basic") == 0)
        test_basic();
    if (argc == 1 || strcmp(argv[1], "unlink") == 0)
        test_unlink_and_sigpipe();
    if (argc == 1 || strcmp(argv[1], "blocking") == 0)
        test_blocking_open();
    if (argc == 1 || strcmp(argv[1], "atomic") == 0)
        test_multiple_and_atomic();
    if (argc == 2 && strcmp(argv[1], "basic") != 0 &&
        strcmp(argv[1], "unlink") != 0 &&
        strcmp(argv[1], "blocking") != 0 &&
        strcmp(argv[1], "atomic") != 0) {
        usage();
        return 2;
    }
    if (failures != 0) {
        fprintf(stderr, "FIFO_SMOKE_FAIL %d\n", failures);
        return 1;
    }
    puts("FIFO_SMOKE_PASS");
    return 0;
}
