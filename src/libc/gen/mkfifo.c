#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int
mkfifo(const char *path, mode_t mode)
{
    return mknod(path, S_IFIFO | (mode & 07777), 0);
}
