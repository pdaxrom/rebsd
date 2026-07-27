#include <sys/param.h>
#include <sys/systm.h>
#include <machine/ramswap.h>

void
kconfig(void)
{
}

void
maltaattach(void)
{
    pipedev = makedev(MIPS_RAMSWAP_MAJOR, MIPS_RAMDISK_VAR_MINOR);
}
