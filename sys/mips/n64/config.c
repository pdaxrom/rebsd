#include <sys/param.h>
#include <sys/systm.h>
#include <machine/ramdisk.h>
#ifdef N64_USB_GDB
#include <machine/n64gdb.h>
#endif

void
kconfig(void)
{
#ifdef N64_USB_GDB
    /*
     * N64 does not walk conf_device_init, so n64cartdriver.d_init is not a
     * usable board attach hook.  Start the debugger transport here, before
     * the VM bootstrap, so the USB device can enumerate during early boot
     * and remains available when VM initialization itself fails.
     */
    n64_gdb_init();
    printf("n64 gdb: USB %s\n",
        n64_gdb_usb_ready() ? "configured" : "waiting for host");
#endif
}

void
nintendoattach(int unit)
{
    (void)unit;
}
