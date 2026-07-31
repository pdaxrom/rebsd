/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_HW_INVENTORY_PROVIDER_H_
#define _SYS_HW_INVENTORY_PROVIDER_H_

#include <sys/hw_inventory.h>

typedef struct kinfo_usb_inventory *(*hw_usb_inventory_provider_t)(void);
typedef struct kinfo_pci_inventory *(*hw_pci_inventory_provider_t)(void);

void hw_inventory_register_usb(hw_usb_inventory_provider_t);
void hw_inventory_register_pci(hw_pci_inventory_provider_t);

#endif
