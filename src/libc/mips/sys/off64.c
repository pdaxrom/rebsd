#include <sys/types.h>
#include <unistd.h>

union off64_words {
    off_t value;
    int word[2];
};

extern off_t __lseek64_words(int, int, int, int);
extern int __truncate64_words(const char *, int, int);
extern int __ftruncate64_words(int, int, int);

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
