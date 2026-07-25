#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>

#include "boot.h"

int noproc __attribute__((weak));
char *panicstr __attribute__((weak));
struct timeval time __attribute__((weak));

void
idle(void)
{
    int state;

    noproc = 1;
    state = splhigh();
    __asm__ volatile ("sti; hlt" : : : "memory");
    splx(state);
}

void __attribute__((weak))
panic(char *message)
{
    panicstr = message;
    i386_early_puts("PANIC: ");
    if (message != (char *)0)
        i386_early_puts(message);
    i386_early_putc('\n');
    for (;;)
        __asm__ volatile ("cli; hlt");
}
