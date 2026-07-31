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

int
main(int argc, char **argv)
{
    struct kinfo_usb_inventory inventory;
    const struct kinfo_usb_device *device;
    int mib[2];
    size_t length;
    unsigned index;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }

    mib[0] = CTL_HW;
    mib[1] = HW_USBDEVICES;
    length = sizeof(inventory);
    if (sysctl(mib, 2, &inventory, &length, NULL, 0) < 0) {
        fprintf(stderr, "lsusb: hw.usbdevices: %s\n", strerror(errno));
        return 1;
    }
    if (length != sizeof(inventory)) {
        fprintf(stderr, "lsusb: incompatible hw.usbdevices snapshot\n");
        return 1;
    }
    if (inventory.kui_count > KINFO_USB_MAXDEVICES) {
        fprintf(stderr, "lsusb: invalid hw.usbdevices snapshot\n");
        return 1;
    }

    for (index = 0; index < inventory.kui_count; ++index) {
        device = &inventory.kui_devices[index];
        printf("Bus %03u Device %03u: ID %04x:%04x\n",
            (unsigned)device->kud_bus,
            (unsigned)device->kud_address,
            (unsigned)device->kud_vendor,
            (unsigned)device->kud_product);
    }
    if (inventory.kui_truncated)
        fprintf(stderr, "lsusb: device list truncated\n");
    return 0;
}
