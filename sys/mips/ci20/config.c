#include <sys/param.h>
#include <sys/systm.h>
#include <machine/ramswap.h>

void ci20_uart_attach(void);
void ci20_delay_init(void);
void ci20_video_attach(void);

void
kconfig(void)
{
    ci20_uart_attach();
    ci20_delay_init();
    ci20_video_attach();
}

void
creatorattach(void)
{
    pipedev = makedev(MIPS_RAMSWAP_MAJOR, MIPS_RAMDISK_VAR_MINOR);
}
