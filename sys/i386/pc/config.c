#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include "disk_bootstrap.h"
#include "ide.h"
#include "pci.h"
#include "usb_pci.h"

void
kconfig(void)
{
}

void
pcattach(int unit)
{
    int error;

    (void)unit;
    error = i386_pci_probe();
    if (error != 0) {
        printf("pci: probe failed, error=%d\n", error);
        return;
    }
    if (i386_ide_probe()) {
        error = i386_disk_attach_ide();
        if (error != 0)
            printf("ide0: disk attach failed, error=%d\n", error);
    }
    error = i386_usb_pci_prepare();
    if (error != 0 && error != ENXIO)
        printf("usb-pci: preparation failed, error=%d\n", error);
    i386_pci_report_summary();
}
