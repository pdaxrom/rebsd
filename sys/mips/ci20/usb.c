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
#include <usb/ehcivar.h>
#include <usb/ohcivar.h>
#include <usb/uhub.h>
#include <usb/usbvar.h>
#include "usb_hw.h"

#define CI20_CPM_BASE               0xb0000000u
#define CI20_GPIO_BASE              0xb0010000u
#define CI20_EHCI_BASE              0xb3490000u
#define CI20_OHCI_BASE              0xb34a0000u
#define CI20_EHCI_IRQ               20u
#define CI20_OHCI_IRQ               5u

#define CI20_GPIO_VBUS_PORT         5u
#define CI20_GPIO_VBUS_PIN          15u
#define CI20_GPIO_PXINTC(n)         (0x18u + (n) * 0x100u)
#define CI20_GPIO_PXMASKS(n)        (0x24u + (n) * 0x100u)
#define CI20_GPIO_PXPAT1C(n)        (0x38u + (n) * 0x100u)
#define CI20_GPIO_PXPAT0S(n)        (0x44u + (n) * 0x100u)
#define CI20_GPIO_PXPAT0C(n)        (0x48u + (n) * 0x100u)

struct ci20_usb_hub_context {
    const char *uch_name;
    unsigned uch_companion;
};

static struct ehci_softc ci20_ehci;
static struct ohci_softc ci20_ohci;
static struct usb_bus ci20_ehci_bus;
static struct usb_bus ci20_ohci_bus;
static struct usb_root_hub ci20_ehci_root_hub;
static struct usb_root_hub ci20_ohci_root_hub;
static struct ci20_usb_hub_context ci20_ehci_hub_context = {
    "ehci0", 0
};
static struct ci20_usb_hub_context ci20_ohci_hub_context = {
    "ohci0", 1
};
static int ci20_usb_hw_ready;

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
    printf("usb-host: init: %s\n", stage);
}

static unsigned
ci20_ehci_read(void *arg, unsigned reg)
{
    (void)arg;
    return ci20_usb_mmio_read(CI20_EHCI_BASE, reg);
}

static void
ci20_ehci_write(void *arg, unsigned reg, unsigned value)
{
    (void)arg;
    ci20_usb_mmio_write(CI20_EHCI_BASE, reg, value);
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
ci20_usb_delay_ms(void *arg, unsigned milliseconds)
{
    (void)arg;
    while (milliseconds-- != 0)
        udelay(1000);
}

static const struct ci20_usb_hw_ops ci20_usb_ops = {
    .cuo_read_cpm = ci20_usb_cpm_read,
    .cuo_write_cpm = ci20_usb_cpm_write,
    .cuo_read_ehci = ci20_ehci_read,
    .cuo_write_ehci = ci20_ehci_write,
    .cuo_set_vbus = ci20_usb_vbus,
    .cuo_delay_us = ci20_usb_delay_us,
    .cuo_trace = ci20_usb_trace,
};

static int
ci20_usb_hw_prepare(void)
{
    int error;

    if (ci20_usb_hw_ready)
        return CI20_USB_HW_OK;
    error = ci20_usb_hw_start(&ci20_usb_ops, 0);
    if (error != CI20_USB_HW_OK)
        return error;
    ci20_usb_hw_ready = 1;
    printf("usb-host: Ci20 VBUS on, cpm clkgr0=%x opcr=%x usbpcr=%x "
        "usbpcr1=%x uhccdr=%x srbc=%x\n",
        ci20_usb_cpm_read(0, CI20_CPM_CLKGR0),
        ci20_usb_cpm_read(0, CI20_CPM_OPCR),
        ci20_usb_cpm_read(0, CI20_CPM_USBPCR),
        ci20_usb_cpm_read(0, CI20_CPM_USBPCR1),
        ci20_usb_cpm_read(0, CI20_CPM_UHCCDR),
        ci20_usb_cpm_read(0, CI20_CPM_SRBC));
    return CI20_USB_HW_OK;
}

static void
ci20_usb_print_device(const char *name, const struct usb_device *device)
{
    const struct usb_interface *interface;
    unsigned i;

    printf("%s: usb addr=%u vendor=%x product=%x config=%u "
        "interfaces=%u\n", name, device->ud_address,
        UGETW(device->ud_desc.idVendor), UGETW(device->ud_desc.idProduct),
        device->ud_config, device->ud_interface_count);
    for (i = 0; i < device->ud_interface_count; ++i) {
        interface = device->ud_interfaces[i];
        printf("%s: if%u class=%u subclass=%u protocol=%u endpoints=%u\n",
            name, interface->ui_desc.bInterfaceNumber,
            interface->ui_desc.bInterfaceClass,
            interface->ui_desc.bInterfaceSubClass,
            interface->ui_desc.bInterfaceProtocol,
            interface->ui_endpoint_count);
    }
}

static void
ci20_usb_hub_event(void *arg, unsigned port,
    enum usb_root_hub_event event, struct usb_device *device,
    usb_error_t status)
{
    struct ci20_usb_hub_context *context;
    const char *speed;

    context = arg;
    if (event == USB_ROOT_HUB_EVENT_ATTACH && device != 0) {
        speed = usb_speed_string(device->ud_speed);
        printf("%s: port%u device attached speed=%s\n",
            context->uch_name, port, speed);
        ci20_usb_print_device(context->uch_name, device);
    } else if (event == USB_ROOT_HUB_EVENT_DETACH) {
        printf("%s: port%u device disconnected\n",
            context->uch_name, port);
        if (context->uch_companion)
            (void)ehci_reclaim_port(&ci20_ehci, port);
    } else {
        printf("%s: port%u %s failed: %s\n", context->uch_name, port,
            usb_root_hub_event_string(event), usb_status_string(status));
        if (context == &ci20_ehci_hub_context &&
            event == USB_ROOT_HUB_EVENT_ENUM_ERROR)
            printf("ehci0: diagnostic cmd=%x status=%x intr=%x "
                "async=%x frame=%x port1=%x\n",
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_USBCMD),
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_USBSTS),
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_USBINTR),
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_ASYNCLISTADDR),
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_FRINDEX),
                ci20_ehci_read(0, ci20_ehci.eh_op_offset +
                    EHCI_PORTSC(1)));
        if (context == &ci20_ehci_hub_context &&
            event == USB_ROOT_HUB_EVENT_ENUM_ERROR)
            printf("ehci0: qh phys=%x link=%x endp=%x hub=%x "
                "cur=%x overlay=%x/%x/%x\n",
                ci20_ehci.eh_last_qh_phys,
                ci20_ehci.eh_last_qh_link,
                ci20_ehci.eh_last_qh_endp,
                ci20_ehci.eh_last_qh_endphub,
                ci20_ehci.eh_last_qh_curqtd,
                ci20_ehci.eh_last_qh_next,
                ci20_ehci.eh_last_qh_altnext,
                ci20_ehci.eh_last_qh_status);
        if (context == &ci20_ehci_hub_context &&
            event == USB_ROOT_HUB_EVENT_ENUM_ERROR)
            printf("ehci0: qtd phys=%x setup=%x/%x/%x/%x "
                "data=%x/%x/%x/%x status=%x/%x/%x/%x\n",
                ci20_ehci.eh_last_qtd_phys,
                ci20_ehci.eh_last_qtd_next[0],
                ci20_ehci.eh_last_qtd_altnext[0],
                ci20_ehci.eh_last_qtd_status[0],
                ci20_ehci.eh_last_qtd_buffer[0],
                ci20_ehci.eh_last_qtd_next[1],
                ci20_ehci.eh_last_qtd_altnext[1],
                ci20_ehci.eh_last_qtd_status[1],
                ci20_ehci.eh_last_qtd_buffer[1],
                ci20_ehci.eh_last_qtd_next[2],
                ci20_ehci.eh_last_qtd_altnext[2],
                ci20_ehci.eh_last_qtd_status[2],
                ci20_ehci.eh_last_qtd_buffer[2]);
        if (context == &ci20_ehci_hub_context &&
            event == USB_ROOT_HUB_EVENT_ENUM_ERROR)
            printf("ehci0: setup bytes=%x/%x/%x/%x/%x/%x/%x/%x\n",
                ci20_ehci.eh_last_setup[0],
                ci20_ehci.eh_last_setup[1],
                ci20_ehci.eh_last_setup[2],
                ci20_ehci.eh_last_setup[3],
                ci20_ehci.eh_last_setup[4],
                ci20_ehci.eh_last_setup[5],
                ci20_ehci.eh_last_setup[6],
                ci20_ehci.eh_last_setup[7]);
        if (context == &ci20_ehci_hub_context &&
            event == USB_ROOT_HUB_EVENT_ENUM_ERROR)
            printf("ehci0: request addr=%u type=%x code=%x value=%x "
                "index=%x length=%u\n", ci20_ehci.eh_last_address,
                ci20_ehci.eh_last_request.bmRequestType,
                ci20_ehci.eh_last_request.bRequest,
                UGETW(ci20_ehci.eh_last_request.wValue),
                UGETW(ci20_ehci.eh_last_request.wIndex),
                UGETW(ci20_ehci.eh_last_request.wLength));
    }
}

static void
ci20_ehci_owner_change(void *arg, unsigned port, int companion,
    unsigned speed)
{
    const char *speed_name;

    (void)arg;
    if (!companion) {
        printf("ehci0: port%u reclaimed from ohci0\n", port);
        return;
    }
    speed_name = usb_speed_string(speed);
    printf("ehci0: port%u handoff to ohci0 speed=%s\n",
        port, speed_name);
}

void
ehciattach(int unit)
{
    struct usb_core *core;
    usb_error_t status;
    unsigned value;
    int error;

    (void)unit;
    printf("ehci0: attach, EHCI phys=13490000\n");
    error = ci20_usb_hw_prepare();
    if (error != CI20_USB_HW_OK) {
        printf("ehci0: JZ4780 clock/PHY initialization failed, error=%d\n",
            error);
        return;
    }
    ehci_softc_init(&ci20_ehci, ci20_ehci_read, ci20_ehci_write,
        ci20_usb_delay_ms, 0);
    ehci_set_owner_callback(&ci20_ehci, ci20_ehci_owner_change, 0);
    core = usb_core_default();
    status = usb_bus_start(core, &ci20_ehci_bus, &ci20_ehci.eh_hcd,
        ci20_usb_delay_ms, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ehci0: controller start failed: %s (cap=%x hcs=%x)\n",
            usb_status_string(status),
            ci20_ehci_read(0, EHCI_CAPLENGTH),
            ci20_ehci_read(0, EHCI_HCSPARAMS));
        return;
    }
    /* Linux repeats this vendor bit after the generic EHCI reset/start. */
    error = ci20_usb_hw_ehci_utmi_width(&ci20_usb_ops, 0);
    if (error != CI20_USB_HW_OK) {
        printf("ehci0: JZ4780 UTMI width setup failed, error=%d\n",
            error);
        usb_bus_stop(&ci20_ehci_bus);
        return;
    }
    value = ci20_ehci_read(0, CI20_EHCI_UTMI_BUS);
    printf("ehci0: JZ4780 UTMI bus=%x width=16-bit\n", value);
    printf("ehci0: EHCI version=%x ports=%u companions=%u/%u "
        "async-control/bulk periodic-interrupt-IN split-transactions\n",
        ci20_ehci.eh_revision,
        ci20_ehci.eh_nports, ci20_ehci.eh_ncomp, ci20_ehci.eh_npcomp);
    status = usb_root_hub_start(&ci20_ehci_root_hub, &ci20_ehci_bus,
        ci20_usb_hub_event, &ci20_ehci_hub_context);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ehci0: root hub start failed: %s\n",
            usb_status_string(status));
        return;
    }
    ci20_intc_unmask_irq(CI20_EHCI_IRQ);
    printf("ehci0: irq %u enabled for async/periodic/root-hub changes\n",
        CI20_EHCI_IRQ);
    if (usb_root_hub_device(&ci20_ehci_root_hub, 1) == 0 &&
        (ci20_ehci_read(0, ci20_ehci.eh_op_offset + EHCI_PORTSC(1)) &
        EHCI_PS_PO) == 0)
        printf("ehci0: port1 powered, no high-speed device; hotplug ready\n");
}

void
ohciattach(int unit)
{
    struct usb_core *core;
    usb_error_t status;
    int error;

    (void)unit;
    printf("ohci0: attach, OHCI phys=134a0000\n");
    error = ci20_usb_hw_prepare();
    if (error != CI20_USB_HW_OK) {
        printf("ohci0: JZ4780 clock/PHY initialization failed, error=%d\n",
            error);
        return;
    }
    ohci_softc_init(&ci20_ohci, ci20_ohci_read, ci20_ohci_write,
        ci20_usb_delay_ms, 0);
    core = usb_core_default();
    status = usb_bus_start(core, &ci20_ohci_bus, &ci20_ohci.oh_hcd,
        ci20_usb_delay_ms, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: controller start failed: %s (revision=%x)\n",
            usb_status_string(status), ci20_ohci_read(0, OHCI_REVISION));
        return;
    }
    printf("ohci0: OHCI revision=%x ports=%u control-polling "
        "periodic-interrupt-IN\n",
        ci20_ohci.oh_revision, ci20_ohci.oh_nports);

    status = usb_root_hub_start(&ci20_ohci_root_hub, &ci20_ohci_bus,
        ci20_usb_hub_event, &ci20_ohci_hub_context);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: root hub start failed: %s\n",
            usb_status_string(status));
        return;
    }
    ci20_intc_unmask_irq(CI20_OHCI_IRQ);
    printf("ohci0: irq %u enabled for periodic/root-hub changes\n",
        CI20_OHCI_IRQ);
    if (usb_root_hub_device(&ci20_ohci_root_hub, 1) == 0)
        printf("ohci0: port1 powered, no device; hotplug ready\n");
}

int
ci20_ehci_intr(void)
{
    return ehci_intr(&ci20_ehci);
}

void
ci20_ehci_irq_storm(void)
{
    unsigned status;
    unsigned enabled;
    unsigned command;
    unsigned port;

    status = ci20_ehci_read(0, ci20_ehci.eh_op_offset + EHCI_USBSTS);
    enabled = ci20_ehci_read(0, ci20_ehci.eh_op_offset + EHCI_USBINTR);
    command = ci20_ehci_read(0, ci20_ehci.eh_op_offset + EHCI_USBCMD);
    port = ci20_ehci_read(0, ci20_ehci.eh_op_offset + EHCI_PORTSC(1));
    printf("ehci0: irq storm temporarily masked status=%x enable=%x "
        "command=%x port1=%x\n", status, enabled, command, port);
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
