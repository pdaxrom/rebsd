#include <unistd.h>

/*
 * ReBSD implements the historical BSD setpgrp(pid, pgrp) system call,
 * while the public declaration retains the POSIX setpgrp(void) interface.
 * Call the common libc entry without a prototype so both arguments reach
 * the kernel on every supported user ABI.
 */
int
deco_setpgid(int pid, int pgrp)
{
    int (*bsd_setpgrp)();

    bsd_setpgrp = (int (*)())setpgrp;
    return ((*bsd_setpgrp)(pid, pgrp));
}
