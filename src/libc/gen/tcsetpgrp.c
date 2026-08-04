#include <sys/types.h>
#include <sys/ioctl.h>

#include <unistd.h>

int
tcsetpgrp(int fd, pid_t pgrp)
{
    int group = pgrp;

    return ioctl(fd, TIOCSPGRP, &group);
}
