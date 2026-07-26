#include <sys/types.h>
#include <unistd.h>

union off64_words {
    off_t value;
    int word[2];
};

typedef char off_t_must_be_two_syscall_words[
    sizeof(off_t) == 2 * sizeof(int) ? 1 : -1];
typedef char off64_t_must_match_off_t[
    sizeof(off64_t) == sizeof(off_t) ? 1 : -1];

extern off_t __lseek64_words(int, int, int, int);
extern int __truncate64_words(const char *, int, int);
extern int __ftruncate64_words(int, int, int);
extern ssize_t __pread_words(int, void *, size_t, int, int);
extern ssize_t __pwrite_words(int, const void *, size_t, int, int);

off_t
lseek(int fd, off_t offset, int whence)
{
    union off64_words arg;

    arg.value = offset;
    return __lseek64_words(fd, arg.word[0], arg.word[1], whence);
}

int
truncate(const char *path, off_t length)
{
    union off64_words arg;

    arg.value = length;
    return __truncate64_words(path, arg.word[0], arg.word[1]);
}

int
ftruncate(int fd, off_t length)
{
    union off64_words arg;

    arg.value = length;
    return __ftruncate64_words(fd, arg.word[0], arg.word[1]);
}

ssize_t
pread(int fd, void *buf, size_t count, off_t offset)
{
    union off64_words arg;

    arg.value = offset;
    return __pread_words(fd, buf, count, arg.word[0], arg.word[1]);
}

ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    union off64_words arg;

    arg.value = offset;
    return __pwrite_words(fd, buf, count, arg.word[0], arg.word[1]);
}

ssize_t
pread64(int fd, void *buf, size_t count, off64_t offset)
{
    return pread(fd, buf, count, offset);
}

ssize_t
pwrite64(int fd, const void *buf, size_t count, off64_t offset)
{
    return pwrite(fd, buf, count, offset);
}
