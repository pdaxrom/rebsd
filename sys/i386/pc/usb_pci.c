/*
 * PCI attachment for the machine-independent USB host-controller stack.
 */

#include "boot.h"
#include "interrupt.h"
#include "io.h"
#include "pci.h"
#include "usb_pci.h"

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <usb/ehcivar.h>
#include <usb/ohcivar.h>
#include <usb/uhcivar.h>
#include <usb/uhub.h>
#include <usb/ukbd.h>
#include <usb/ums.h>
#include <usb/umass.h>
#include <usb/usbvar.h>
#define I386_PCI_USB_REVISION         0x60u
#define I386_PCI_UHCI_LEGACY          0xc0u
#define I386_PCI_UHCI_PIRQ_ENABLE     0x00002000u
#define I386_USB_HC_REGISTER_BYTES    256u
#define I386_USB_HC_IO_BYTES          32u

struct i386_usb_pci_controller {
    struct pci_device up_function;
    struct pci_resource up_resource;
    const char *up_name;
    i386_u8 up_revision;
    unsigned up_irq;
    unsigned up_present;
    unsigned up_prepared;
    unsigned up_attached;
    unsigned up_interrupts;
};

static struct i386_usb_pci_controller i386_ehci_pci;
static struct i386_usb_pci_controller i386_ohci_pci;
static struct i386_usb_pci_controller i386_uhci_pci;
static struct ehci_softc i386_ehci;
static struct ohci_softc i386_ohci;
static struct uhci_softc i386_uhci;
static struct usb_bus i386_ehci_bus;
static struct usb_bus i386_ohci_bus;
static struct usb_bus i386_uhci_bus;
static struct usb_root_hub i386_ehci_root_hub;
static struct usb_root_hub i386_ohci_root_hub;
static struct usb_root_hub i386_uhci_root_hub;

static unsigned
i386_usb_pci_read(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return pci_resource_read32(&controller->up_resource, reg);
}

static void
i386_usb_pci_write(void *arg, unsigned reg, unsigned value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    pci_resource_write32(&controller->up_resource, reg, value);
}

static unsigned short
i386_usb_pci_io_read_2(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return pci_resource_read16(&controller->up_resource, reg);
}

static void
i386_usb_pci_io_write_2(void *arg, unsigned reg, unsigned short value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    pci_resource_write16(&controller->up_resource, reg, value);
}

static unsigned
i386_usb_pci_io_read_4(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return pci_resource_read32(&controller->up_resource, reg);
}

static void
i386_usb_pci_io_write_4(void *arg, unsigned reg, unsigned value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    pci_resource_write32(&controller->up_resource, reg, value);
}

static void
i386_usb_pci_delay_ms(void *arg, unsigned milliseconds)
{
    i386_u32 ticks;

    (void)arg;
    if (milliseconds == 0)
        return;
    ticks = (milliseconds * I386_PIT_HZ + 999u) / 1000u;
    if (ticks == 0)
        ticks = 1;
    i386_pit_wait(ticks);
}

static void
i386_usb_hub_event(void *arg, unsigned port,
    enum usb_root_hub_event event, struct usb_device *device,
    usb_error_t status)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    if (event == USB_ROOT_HUB_EVENT_ATTACH && device != 0) {
        printf("%s: port%u device attached speed=%s "
            "vendor=%x product=%x\n", controller->up_name, port,
            usb_speed_string(device->ud_speed),
            UGETW(device->ud_desc.idVendor),
            UGETW(device->ud_desc.idProduct));
    } else if (event == USB_ROOT_HUB_EVENT_DETACH) {
        printf("%s: port%u device disconnected\n",
            controller->up_name, port);
    } else {
        printf("%s: port%u %s failed: %s\n", controller->up_name,
            port, usb_root_hub_event_string(event),
            usb_status_string(status));
    }
}

static int
i386_ehci_interrupt(void *arg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    if (!ehci_intr(&i386_ehci))
        return 0;
    ++controller->up_interrupts;
    return 1;
}

static int
i386_ohci_interrupt(void *arg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    if (!ohci_intr(&i386_ohci))
        return 0;
    ++controller->up_interrupts;
    return 1;
}

static int
i386_uhci_interrupt(void *arg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    if (!uhci_intr(&i386_uhci))
        return 0;
    ++controller->up_interrupts;
    return 1;
}

static int
i386_usb_pci_prepare_mmio_controller(
    struct i386_usb_pci_controller *controller,
    const char *name, i386_u8 programming_interface)
{
    struct pci_bus *bus;
    struct pci_device function;
    i386_u32 interrupt;
    int error;

    if (controller->up_prepared)
        return 0;
    bus = i386_pci_bus();
    if (bus == 0 || !pci_find_class(bus, PCI_CLASS_SERIAL_BUS,
        PCI_SUBCLASS_USB, programming_interface, &function))
        return ENXIO;
    controller->up_present = 1;
    error = pci_map_bar(&function, 0, I386_USB_HC_REGISTER_BYTES,
        &controller->up_resource);
    if (error != 0)
        return error;
    if (controller->up_resource.pr_type != PCI_RESOURCE_MEMORY)
        return EINVAL;
    interrupt = pci_config_read32(&function, PCI_CONFIG_INTERRUPT) & 0xffu;
    if (interrupt == 0 || interrupt >= I386_IRQ_COUNT)
        return EINVAL;
    error = pci_device_enable(&function,
        PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    if (error != 0)
        return error;

    controller->up_function = function;
    controller->up_name = name;
    controller->up_irq = interrupt;
    controller->up_prepared = 1;
    i386_early_puts(name);
    i386_early_puts(": pci-id=");
    i386_early_put_hex32(((i386_u32)function.pd_vendor << 16) |
        function.pd_product);
    i386_early_puts(" mmio=");
    i386_early_put_hex32((i386_u32)controller->up_resource.pr_address);
    i386_early_puts(" irq=");
    i386_early_put_hex32(controller->up_irq);
    i386_early_putc('\n');
    return 0;
}

static int
i386_usb_pci_prepare_uhci(void)
{
    struct pci_bus *bus;
    struct pci_device function;
    i386_u32 interrupt;
    i386_u32 legacy;
    i386_u32 revision;
    int error;

    if (i386_uhci_pci.up_prepared)
        return 0;
    bus = i386_pci_bus();
    if (bus == 0 || !pci_find_class(bus, PCI_CLASS_SERIAL_BUS,
        PCI_SUBCLASS_USB, PCI_INTERFACE_UHCI, &function))
        return ENXIO;
    i386_uhci_pci.up_present = 1;
    error = pci_map_bar(&function, 4, I386_USB_HC_IO_BYTES,
        &i386_uhci_pci.up_resource);
    if (error != 0)
        return error;
    if (i386_uhci_pci.up_resource.pr_type != PCI_RESOURCE_IO)
        return EINVAL;
    interrupt = pci_config_read32(&function, PCI_CONFIG_INTERRUPT) & 0xffu;
    if (interrupt == 0 || interrupt >= I386_IRQ_COUNT)
        return EINVAL;
    error = pci_device_enable(&function, PCI_COMMAND_IO |
        PCI_COMMAND_MASTER);
    if (error != 0)
        return error;

    legacy = I386_PCI_UHCI_PIRQ_ENABLE;
    pci_config_write32(&function, I386_PCI_UHCI_LEGACY, legacy);
    revision = pci_config_read32(&function, I386_PCI_USB_REVISION);

    i386_uhci_pci.up_function = function;
    i386_uhci_pci.up_name = "uhci0";
    i386_uhci_pci.up_revision = (i386_u8)(revision & 0xffu);
    i386_uhci_pci.up_irq = interrupt;
    i386_uhci_pci.up_prepared = 1;
    i386_usb_pci_io_write_2(&i386_uhci_pci, UHCI_INTR, 0);
    i386_usb_pci_io_write_2(&i386_uhci_pci, UHCI_STS,
        i386_usb_pci_io_read_2(&i386_uhci_pci, UHCI_STS) &
        UHCI_STS_ACK);

    i386_early_puts("uhci0: pci-id=");
    i386_early_put_hex32(((i386_u32)function.pd_vendor << 16) |
        function.pd_product);
    i386_early_puts(" io=");
    i386_early_put_hex32((i386_u32)i386_uhci_pci.up_resource.pr_address);
    i386_early_puts(" irq=");
    i386_early_put_hex32(interrupt);
    i386_early_puts(" revision=");
    i386_early_put_hex32(i386_uhci_pci.up_revision);
    i386_early_putc('\n');
    return 0;
}

int
i386_usb_pci_prepare(void)
{
    int ehci_error;
    int ohci_error;
    int uhci_error;

    uhci_error = i386_usb_pci_prepare_uhci();
    if (uhci_error != 0 && uhci_error != ENXIO)
        return uhci_error;
    ohci_error = i386_usb_pci_prepare_mmio_controller(&i386_ohci_pci,
        "ohci0", PCI_INTERFACE_OHCI);
    if (ohci_error != 0 && ohci_error != ENXIO)
        return ohci_error;
    ehci_error = i386_usb_pci_prepare_mmio_controller(&i386_ehci_pci,
        "ehci0", PCI_INTERFACE_EHCI);
    if (ehci_error != 0 && ehci_error != ENXIO)
        return ehci_error;
    if (uhci_error == ENXIO && ohci_error == ENXIO &&
        ehci_error == ENXIO)
        return ENXIO;
    return 0;
}

static int
i386_uhci_attach(void)
{
    struct usb_core *core;
    usb_error_t status;

    if (!i386_uhci_pci.up_prepared)
        return ENXIO;
    if (i386_uhci_pci.up_attached)
        return 0;
    uhci_softc_init(&i386_uhci, i386_usb_pci_io_read_2,
        i386_usb_pci_io_write_2, i386_usb_pci_io_read_4,
        i386_usb_pci_io_write_4, i386_usb_pci_delay_ms,
        &i386_uhci_pci);
    core = usb_core_default();
    status = usb_bus_start(core, &i386_uhci_bus, &i386_uhci.uh_hcd,
        i386_usb_pci_delay_ms, &i386_uhci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("uhci0: controller start failed: %s\n",
            usb_status_string(status));
        return EIO;
    }
    printf("uhci0: UHCI revision=%x ports=%u "
        "control/bulk/interrupt\n", i386_uhci_pci.up_revision,
        UHCI_ROOT_PORTS);
    status = usb_root_hub_start(&i386_uhci_root_hub,
        &i386_uhci_bus, i386_usb_hub_event, &i386_uhci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("uhci0: root hub start failed: %s\n",
            usb_status_string(status));
        usb_bus_stop(&i386_uhci_bus);
        return EIO;
    }
    if (pci_interrupt_establish(&i386_uhci_pci.up_function,
        i386_uhci_interrupt, &i386_uhci_pci) != 0) {
        usb_root_hub_stop(&i386_uhci_root_hub);
        usb_bus_stop(&i386_uhci_bus);
        return ENOMEM;
    }
    i386_uhci_pci.up_attached = 1;
    printf("uhci0: irq %u enabled\n", i386_uhci_pci.up_irq);
    return 0;
}

static int
i386_ehci_attach(void)
{
    struct usb_core *core;
    usb_error_t status;

    if (!i386_ehci_pci.up_prepared)
        return ENXIO;
    if (i386_ehci_pci.up_attached)
        return 0;

    ehci_softc_init(&i386_ehci, i386_usb_pci_read, i386_usb_pci_write,
        i386_usb_pci_delay_ms, &i386_ehci_pci);
    core = usb_core_default();
    status = usb_bus_start(core, &i386_ehci_bus, &i386_ehci.eh_hcd,
        i386_usb_pci_delay_ms, &i386_ehci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ehci0: controller start failed: %s\n",
            usb_status_string(status));
        return EIO;
    }
    printf("ehci0: EHCI version=%x ports=%u "
        "control/bulk/interrupt\n",
        i386_ehci.eh_revision, i386_ehci.eh_nports);
    status = usb_root_hub_start(&i386_ehci_root_hub,
        &i386_ehci_bus, i386_usb_hub_event, &i386_ehci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ehci0: root hub start failed: %s\n",
            usb_status_string(status));
        usb_bus_stop(&i386_ehci_bus);
        return EIO;
    }
    if (pci_interrupt_establish(&i386_ehci_pci.up_function,
        i386_ehci_interrupt, &i386_ehci_pci) != 0) {
        usb_root_hub_stop(&i386_ehci_root_hub);
        usb_bus_stop(&i386_ehci_bus);
        return ENOMEM;
    }
    i386_ehci_pci.up_attached = 1;
    printf("ehci0: irq %u enabled\n", i386_ehci_pci.up_irq);
    return 0;
}

static int
i386_ohci_attach(void)
{
    struct usb_core *core;
    usb_error_t status;

    if (!i386_ohci_pci.up_prepared)
        return ENXIO;
    if (i386_ohci_pci.up_attached)
        return 0;

    ohci_softc_init(&i386_ohci, i386_usb_pci_read, i386_usb_pci_write,
        i386_usb_pci_delay_ms, &i386_ohci_pci);
    core = usb_core_default();
    status = usb_bus_start(core, &i386_ohci_bus, &i386_ohci.oh_hcd,
        i386_usb_pci_delay_ms, &i386_ohci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: controller start failed: %s\n",
            usb_status_string(status));
        return EIO;
    }
    printf("ohci0: OHCI revision=%x ports=%u "
        "control-polling periodic-interrupt-IN\n",
        i386_ohci.oh_revision, i386_ohci.oh_nports);
    status = usb_root_hub_start(&i386_ohci_root_hub,
        &i386_ohci_bus, i386_usb_hub_event, &i386_ohci_pci);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("ohci0: root hub start failed: %s\n",
            usb_status_string(status));
        usb_bus_stop(&i386_ohci_bus);
        return EIO;
    }
    if (pci_interrupt_establish(&i386_ohci_pci.up_function,
        i386_ohci_interrupt, &i386_ohci_pci) != 0) {
        usb_root_hub_stop(&i386_ohci_root_hub);
        usb_bus_stop(&i386_ohci_bus);
        return ENOMEM;
    }
    i386_ohci_pci.up_attached = 1;
    printf("ohci0: irq enabled line=%u\n", i386_ohci_pci.up_irq);
    return 0;
}

void
ehciattach(int unit)
{
    int error;

    (void)unit;
    error = i386_ehci_attach();
    if (error != 0)
        printf("ehci0: attach failed, error=%d\n", error);
}

void
ohciattach(int unit)
{
    int error;

    (void)unit;
    error = i386_ohci_attach();
    if (error != 0)
        printf("ohci0: attach failed, error=%d\n", error);
}

void
uhciattach(int unit)
{
    int error;

    (void)unit;
    error = i386_uhci_attach();
    if (error != 0)
        printf("uhci0: attach failed, error=%d\n", error);
}

int
i386_usb_attach(void)
{
    int error;

    if (!i386_ehci_pci.up_present && !i386_ohci_pci.up_present &&
        !i386_uhci_pci.up_present)
        return 0;
    if ((i386_ehci_pci.up_present && !i386_ehci_pci.up_prepared) ||
        (i386_ohci_pci.up_present && !i386_ohci_pci.up_prepared) ||
        (i386_uhci_pci.up_present && !i386_uhci_pci.up_prepared))
        return ENXIO;
    error = i386_dma_attach();
    if (error != 0)
        return error;
    usbattach(0);
    umassattach(0);
    uhubattach(0);
    ukbdattach(0);
    umsattach(0);
    if (i386_uhci_pci.up_present) {
        error = i386_uhci_attach();
        if (error != 0)
            return error;
    }
    if (i386_ohci_pci.up_present) {
        error = i386_ohci_attach();
        if (error != 0)
            return error;
    }
    if (i386_ehci_pci.up_present)
        return i386_ehci_attach();
    return 0;
}
