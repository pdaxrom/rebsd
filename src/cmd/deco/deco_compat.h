#ifndef DECO_COMPAT_H
#define DECO_COMPAT_H

#include <sys/ioctl.h>
#include <unistd.h>

int killpg(int pgrp, int sig);

#if defined(__GNUC__)
#define DECO_UNUSED __attribute__((unused))
#else
#define DECO_UNUSED
#endif

static DECO_UNUSED int
deco_tcsetpgrp(int fd, int pgrp)
{
	return ioctl(fd, TIOCSPGRP, &pgrp);
}

static DECO_UNUSED char *
deco_getcwd(char *buf, int size)
{
	(void)size;
	return getwd(buf);
}

int deco_setpgid(int pid, int pgrp);

#define tcsetpgrp deco_tcsetpgrp
#define getcwd deco_getcwd
#define setpgid deco_setpgid

#endif
