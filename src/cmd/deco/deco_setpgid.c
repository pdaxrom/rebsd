#include <unistd.h>

int
deco_setpgid(int pid, int pgrp)
{
    return setpgid(pid, pgrp);
}
