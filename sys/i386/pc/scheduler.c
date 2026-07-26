#include <sys/param.h>
#include <sys/systm.h>

#include "boot.h"

extern int noproc;

void
idle(void)
{
    int state;

    noproc = 1;
    state = splhigh();
    __asm__ volatile ("sti; hlt" : : : "memory");
    splx(state);
}
