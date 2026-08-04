#include <sys/param.h>
#include <sys/systm.h>
#include <machine/ramdisk.h>

void
kconfig(void)
{
}

void
maltaattach(int unit)
{
    (void)unit;
    pipedev = makedev(MIPS_RAMDISK_MAJOR, MIPS_RAMDISK_VAR_MINOR);
}
