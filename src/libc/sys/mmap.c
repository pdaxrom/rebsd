#include <sys/types.h>
#include <sys/mman.h>

union mmap_off_words {
    off_t value;
    int word[2];
};

extern void *__mmap_words(void *, size_t, int, int, int, int, int, int);

void *
mmap(void *address, size_t length, int protection, int flags, int fd,
    off_t offset)
{
    union mmap_off_words arg;

    arg.value = offset;
    return __mmap_words(address, length, protection, flags, fd, 0,
        arg.word[0], arg.word[1]);
}
