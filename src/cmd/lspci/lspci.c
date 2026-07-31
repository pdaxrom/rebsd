/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *
pci_class_name(unsigned class_code, unsigned subclass)
{
    if (class_code == 0x01 && subclass == 0x01)
        return "IDE interface";
    if (class_code == 0x02 && subclass == 0x00)
        return "Ethernet controller";
    if (class_code == 0x03 && subclass == 0x00)
        return "VGA compatible controller";
    if (class_code == 0x06 && subclass == 0x00)
        return "Host bridge";
    if (class_code == 0x06 && subclass == 0x01)
        return "ISA bridge";
    if (class_code == 0x06 && subclass == 0x04)
        return "PCI bridge";
    if (class_code == 0x0c && subclass == 0x03)
        return "USB controller";
    switch (class_code) {
    case 0x01:
        return "Mass storage controller";
    case 0x02:
        return "Network controller";
    case 0x03:
        return "Display controller";
    case 0x04:
        return "Multimedia controller";
    case 0x06:
        return "Bridge";
    case 0x0c:
        return "Serial bus controller";
    default:
        return "Unclassified device";
    }
}

static void
usage(void)
{
    fprintf(stderr, "usage: lspci [-n]\n");
}

int
main(int argc, char **argv)
{
    struct kinfo_pci_inventory inventory;
    const struct kinfo_pci_device *device;
    int mib[2];
    int numeric;
    int ch;
    size_t length;
    unsigned index;

    numeric = 0;
    while ((ch = getopt(argc, argv, "n")) != -1) {
        switch (ch) {
        case 'n':
            numeric = 1;
            break;
        default:
            usage();
            return 1;
        }
    }
    if (optind != argc) {
        usage();
        return 1;
    }

    mib[0] = CTL_HW;
    mib[1] = HW_PCIDEVICES;
    length = sizeof(inventory);
    if (sysctl(mib, 2, &inventory, &length, NULL, 0) < 0) {
        fprintf(stderr, "lspci: hw.pcidevices: %s\n", strerror(errno));
        return 1;
    }
    if (length != sizeof(inventory)) {
        fprintf(stderr, "lspci: incompatible hw.pcidevices snapshot\n");
        return 1;
    }
    if (inventory.kpi_count > KINFO_PCI_MAXDEVICES) {
        fprintf(stderr, "lspci: invalid hw.pcidevices snapshot\n");
        return 1;
    }

    for (index = 0; index < inventory.kpi_count; ++index) {
        device = &inventory.kpi_devices[index];
        printf("%02x:%02x.%u ",
            (unsigned)device->kpd_bus,
            (unsigned)device->kpd_device,
            (unsigned)device->kpd_function);
        if (numeric)
            printf("%02x%02x: ", (unsigned)device->kpd_class,
                (unsigned)device->kpd_subclass);
        else
            printf("%s: ", pci_class_name(device->kpd_class,
                device->kpd_subclass));
        printf("%04x:%04x (rev %02x)\n",
            (unsigned)device->kpd_vendor,
            (unsigned)device->kpd_product,
            (unsigned)device->kpd_revision);
    }
    if (inventory.kpi_truncated)
        fprintf(stderr, "lspci: function list truncated\n");
    return 0;
}
