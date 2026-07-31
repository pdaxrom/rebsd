/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_HW_INVENTORY_H_
#define _SYS_HW_INVENTORY_H_

/* Pointer-free hardware-bus snapshots used by lsusb and lspci. */
#define KINFO_USB_MAXDEVICES 16
struct kinfo_usb_device {
    unsigned char kud_bus;
    unsigned char kud_address;
    unsigned char kud_port;
    unsigned char kud_parent_address;
    unsigned char kud_speed;
    unsigned char kud_depth;
    unsigned char kud_config;
    unsigned char kud_interface_count;
    unsigned char kud_device_class;
    unsigned char kud_device_subclass;
    unsigned char kud_device_protocol;
    unsigned char kud_reserved;
    unsigned short kud_vendor;
    unsigned short kud_product;
    unsigned short kud_release;
};
struct kinfo_usb_inventory {
    unsigned int kui_count;
    unsigned int kui_truncated;
    struct kinfo_usb_device kui_devices[KINFO_USB_MAXDEVICES];
};

#define KINFO_PCI_MAXDEVICES 128
struct kinfo_pci_device {
    unsigned char kpd_bus;
    unsigned char kpd_device;
    unsigned char kpd_function;
    unsigned char kpd_class;
    unsigned char kpd_subclass;
    unsigned char kpd_programming_interface;
    unsigned char kpd_revision;
    unsigned char kpd_reserved;
    unsigned short kpd_vendor;
    unsigned short kpd_product;
};
struct kinfo_pci_inventory {
    unsigned int kpi_count;
    unsigned int kpi_truncated;
    struct kinfo_pci_device kpi_devices[KINFO_PCI_MAXDEVICES];
};

#endif
