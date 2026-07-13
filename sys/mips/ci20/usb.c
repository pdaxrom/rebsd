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
#include <dev/usb/ohcivar.h>
#include <dev/usb/uhub.h>
#include <dev/usb/usbvar.h>
#include "usb_hw.h"

#define CI20_CPM_BASE               0xb0000000u
#define CI20_GPIO_BASE              0xb0010000u
#define CI20_OHCI_BASE              0xb34a0000u
#define CI20_OHCI_IRQ               5u

#define CI20_GPIO_VBUS_PORT         5u
#define CI20_GPIO_VBUS_PIN          15u
#define CI20_GPIO_PXINTC(n)         (0x18u + (n) * 0x100u)
#define CI20_GPIO_PXMASKS(n)        (0x24u + (n) * 0x100u)
#define CI20_GPIO_PXPAT1C(n)        (0x38u + (n) * 0x100u)
#define CI20_GPIO_PXPAT0S(n)        (0x44u + (n) * 0x100u)
#define CI20_GPIO_PXPAT0C(n)        (0x48u + (n) * 0x100u)

static struct ohci_softc ci20_ohci;
static struct usb_bus ci20_usb_bus;
static struct usb_root_hub ci20_usb_root_hub;

extern void udelay(unsigned);
extern void ci20_intc_unmask_irq(unsigned);

static volatile unsigned *
ci20_usb_reg(unsigned base, unsigned offset)
{
    return (volatile unsigned *)(base + offset);
}

static unsigned
ci20_usb_mmio_read(unsigned base, unsigned offset)
{
    return *ci20_usb_reg(base, offset);
}

static void
ci20_usb_mmio_write(unsigned base, unsigned offset, unsigned value)
{
    *ci20_usb_reg(base, offset) = value;
    asm volatile ("sync" ::: "memory");
}

static unsigned
ci20_usb_cpm_read(void *arg, unsigned reg)
{
    (void)arg;
    return ci20_usb_mmio_read(CI20_CPM_BASE, reg);
}

static void
ci20_usb_cpm_write(void *arg, unsigned reg, unsigned value)
{
    (void)arg;
    ci20_usb_mmio_write(CI20_CPM_BASE, reg, value);
}

static void
ci20_usb_vbus(void *arg, int on)
{
    unsigned bit;

    (void)arg;
    bit = 1u << CI20_GPIO_VBUS_PIN;
    ci20_usb_mmio_write(CI20_GPIO_BASE,
        CI20_GPIO_PXINTC(CI20_GPIO_VBUS_PORT), bit);
    ci20_usb_mmio_write(CI20_GPIO_BASE,
        CI20_GPIO_PXMASKS(CI20_GPIO_VBUS_PORT), bit);
    ci20_usb_mmio_write(CI20_GPIO_BASE,
        CI20_GPIO_PXPAT1C(CI20_GPIO_VBUS_PORT), bit);
    ci20_usb_mmio_write(CI20_GPIO_BASE,
        on ? CI20_GPIO_PXPAT0S(CI20_GPIO_VBUS_PORT) :
        CI20_GPIO_PXPAT0C(CI20_GPIO_VBUS_PORT), bit);
}

static void
ci20_usb_delay_us(void *arg, unsigned usec)
{
    (void)arg;
    udelay(usec);
}

static void
ci20_usb_trace(void *arg, const char *stage)
{
    (void)arg;
    printf("ohci0: init: %s\n", stage);
}

static unsigned
ci20_ohci_read(void *arg, unsigned reg)
{
    (void)arg;
    return ci20_usb_mmio_read(CI20_OHCI_BASE, reg);
}

static void
ci20_ohci_write(void *arg, unsigned reg, unsigned value)
{
    (void)arg;
    ci20_usb_mmio_write(CI20_OHCI_BASE, reg, value);
}

static void
ci20_ohci_delay_ms(void *arg, unsigned milliseconds)
{
    (void)arg;
    while (milliseconds-- != 0)
        udelay(1000);
}

static const struct ci20_usb_hw_ops ci20_usb_ops = {
    .cuo_read_cpm = ci20_usb_cpm_read,
    .cuo_write_cpm = ci20_usb_cpm_write,
    .cuo_set_vbus = ci20_usb_vbus,
    .cuo_delay_us = ci20_usb_delay_us,
    .cuo_trace = ci20_usb_trace,
};

static void
ci20_usb_print_device(const struct usb_device *device)
{
    const struct usb_interface *interface;
    unsigned i;

    printf("ohci0: usb addr=%u vendor=%x product=%x config=%u "
        "interfaces=%u\n", device->ud_address,
        UGETW(device->ud_desc.idVendor), UGETW(device->ud_desc.idProduct),
        device->ud_config, device->ud_interface_count);
    for (i = 0; i < device->ud_interface_count; ++i) {
        interface = device->ud_interfaces[i];
        printf("ohci0: if%u class=%u subclass=%u protocol=%u endpoints=%u\n",
            interface->ui_desc.bInterfaceNumber,
            interface->ui_desc.bInterfaceClass,
            interface->ui_desc.bInterfaceSubClass,
            interface->ui_desc.bInterfaceProtocol,
            interface->ui_endpoint_count);
    }
}

static const char *
ci20_usb_hub_operation(enum usb_root_hub_event event)
{
    switch (event) {
    case USB_ROOT_HUB_EVENT_STATUS_ERROR:
        return "status";
    case USB_ROOT_HUB_EVENT_POWER_ERROR:
        return "power";
    case USB_ROOT_HUB_EVENT_RESET_ERROR:
        return "reset";
    case USB_ROOT_HUB_EVENT_ENUM_ERROR:
        return "enumeration";
    default:
        return "unknown";
    }
}

static void
ci20_usb_hub_event(void *arg, unsigned port,
    enum usb_root_hub_event event, struct usb_device *device,
    usb_error_t status)
{
    const char *speed;

    (void)arg;
    if (event == USB_ROOT_HUB_EVENT_ATTACH && device != 0) {
        speed = device->ud_speed == USB_SPEED_LOW ? "low" :
            device->ud_speed == USB_SPEED_HIGH ? "high" : "full";
        printf("ohci0: port%u device attached speed=%s\n", port, speed);
        ci20_usb_print_device(device);
    } else if (event == USB_ROOT_HUB_EVENT_DETACH) {
        printf("ohci0: port%u device disconnected\n", port);
    } else {
        printf("ohci0: port%u %s failed: %s\n", port,
            ci20_usb_hub_operation(event), usb_status_string(status));
    }
}

void
ohciattach(int unit)
{
    struct usb_core *core;
    usb_error_t status;
    int error;

    (void)unit;
    printf("ohci0: attach, OHCI phys=134a0000\n");
    error = ci20_usb_hw_start(&ci20_usb_ops, 0);
    if (error != CI20_USB_HW_OK) {
        printf("ohci0: JZ4780 clock/PHY initialization failed, error=%d\n",
            error);
        return;
    }
    printf("ohci0: Ci20 VBUS on, cpm clkgr0=%x opcr=%x usbpcr=%x "
        "usbpcr1=%x uhccdr=%x srbc=%x\n",
        ci20_usb_cpm_read(0, CI20_CPM_CLKGR0),
        ci20_usb_cpm_read(0, CI20_CPM_OPCR),
        ci20_usb_cpm_read(0, CI20_CPM_USBPCR),
        ci20_usb_cpm_read(0, CI20_CPM_USBPCR1),
        ci20_usb_cpm_read(0, CI20_CPM_UHCCDR),
        ci20_usb_cpm_read(0, CI20_CPM_SRBC));

    ohci_softc_init(&ci20_ohci, ci20_ohci_read, ci20_ohci_write,
        ci20_ohci_delay_ms, 0);
    core = usb_core_default();
    status = usb_bus_start(core, &ci20_usb_bus, &ci20_ohci.oh_hcd,
        ci20_ohci_delay_ms, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: controller start failed: %s (revision=%x)\n",
            usb_status_string(status), ci20_ohci_read(0, OHCI_REVISION));
        return;
    }
    printf("ohci0: OHCI revision=%x ports=%u control-polling "
        "periodic-interrupt-IN\n",
        ci20_ohci.oh_revision, ci20_ohci.oh_nports);

    status = usb_root_hub_start(&ci20_usb_root_hub, &ci20_usb_bus,
        ci20_usb_hub_event, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: root hub start failed: %s\n",
            usb_status_string(status));
        return;
    }
    ci20_intc_unmask_irq(CI20_OHCI_IRQ);
    printf("ohci0: irq %u enabled for periodic/root-hub changes\n",
        CI20_OHCI_IRQ);
    if (usb_root_hub_device(&ci20_usb_root_hub, 1) == 0)
        printf("ohci0: port1 powered, no device; hotplug ready\n");
}

int
ci20_ohci_intr(void)
{
    return ohci_intr(&ci20_ohci);
}

void
ci20_ohci_irq_storm(void)
{
    unsigned status;
    unsigned enabled;
    unsigned control;
    unsigned port;

    status = ci20_ohci_read(0, OHCI_INTERRUPT_STATUS);
    enabled = ci20_ohci_read(0, OHCI_INTERRUPT_ENABLE);
    control = ci20_ohci_read(0, OHCI_CONTROL);
    port = ci20_ohci_read(0, OHCI_RH_PORT_STATUS(1));
    ci20_ohci_write(0, OHCI_INTERRUPT_DISABLE,
        OHCI_MIE | OHCI_ALL_INTRS);
    printf("ohci0: irq storm quarantined status=%x enable=%x "
        "control=%x port1=%x\n", status, enabled, control, port);
}
