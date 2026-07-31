/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/hw_inventory_provider.h>
#include <usb/usb_task.h>
#include <usb/usbvar.h>

static struct usb_core usb_default_core;
static struct kinfo_usb_inventory usb_inventory;

static unsigned
usb_inventory_bus_number(struct usb_bus **buses, unsigned *bus_count,
    struct usb_bus *bus)
{
    unsigned index;

    for (index = 0; index < *bus_count; ++index)
        if (buses[index] == bus)
            return index + 1;
    if (*bus_count >= USB_MAX_DEVICES)
        return 0;
    buses[*bus_count] = bus;
    ++*bus_count;
    return *bus_count;
}

static struct kinfo_usb_inventory *
usb_inventory_snapshot(void)
{
    struct kinfo_usb_device *entry;
    struct usb_device *device;
    struct usb_bus *buses[USB_MAX_DEVICES];
    unsigned bus_count;
    unsigned index;

    bzero(&usb_inventory, sizeof(usb_inventory));
    bus_count = 0;
    for (index = 0; index < USB_MAX_DEVICES; ++index) {
        device = &usb_default_core.uc_devices[index];
        if (!device->ud_used || !device->ud_connected)
            continue;
        if (usb_inventory.kui_count >= KINFO_USB_MAXDEVICES) {
            usb_inventory.kui_truncated = 1;
            continue;
        }
        entry = &usb_inventory.kui_devices[usb_inventory.kui_count++];
        entry->kud_bus = (unsigned char)usb_inventory_bus_number(buses,
            &bus_count, device->ud_bus);
        entry->kud_address = device->ud_address;
        entry->kud_port = device->ud_port;
        entry->kud_parent_address = device->ud_parent_hub != NULL ?
            device->ud_parent_hub->ud_address : 0;
        entry->kud_speed = device->ud_speed;
        entry->kud_depth = device->ud_depth;
        entry->kud_config = device->ud_config;
        entry->kud_interface_count =
            (unsigned char)device->ud_interface_count;
        entry->kud_device_class = device->ud_desc.bDeviceClass;
        entry->kud_device_subclass = device->ud_desc.bDeviceSubClass;
        entry->kud_device_protocol = device->ud_desc.bDeviceProtocol;
        entry->kud_vendor = UGETW(device->ud_desc.idVendor);
        entry->kud_product = UGETW(device->ud_desc.idProduct);
        entry->kud_release = UGETW(device->ud_desc.bcdDevice);
    }
    return &usb_inventory;
}

struct usb_core *
usb_core_default(void)
{
    return &usb_default_core;
}

void
usbattach(int unit)
{
    (void)unit;
    printf("usb0: initializing core\n");
    usb_task_system_init();
    usb_core_init(&usb_default_core);
    hw_inventory_register_usb(usb_inventory_snapshot);
    printf("usb0: core ready\n");
}
