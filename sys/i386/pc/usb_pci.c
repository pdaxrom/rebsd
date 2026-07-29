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
#include <vm/pmap.h>

#define I386_PCI_COMMAND_STATUS       0x04u
#define I386_PCI_BAR0                 0x10u
#define I386_PCI_BAR4                 0x20u
#define I386_PCI_INTERRUPT_LINE       0x3cu
#define I386_PCI_USB_REVISION         0x60u
#define I386_PCI_UHCI_LEGACY          0xc0u
#define I386_PCI_UHCI_PIRQ_ENABLE     0x00002000u
#define I386_PCI_BAR_IO               0x01u
#define I386_PCI_BAR_IO_ADDRESS       0xfffffffcu
#define I386_PCI_BAR_MEMORY_TYPE      0x06u
#define I386_PCI_BAR_MEMORY_32        0x00u
#define I386_PCI_BAR_MEMORY_ADDRESS   0xfffffff0u
#define I386_USB_HC_MMIO_BYTES        4096u

struct i386_usb_pci_controller {
    struct i386_pci_function up_function;
    const char *up_name;
    volatile unsigned char *up_mmio;
    i386_u32 up_mmio_phys;
    i386_u16 up_io_base;
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

static void *
i386_usb_pci_map_mmio(i386_u32 paddr, i386_u32 size)
{
    void *first;
    void *mapping;
    i386_u32 offset;
    i386_u32 physical;
    i386_u32 span;
    i386_u32 mapped;

    if (size == 0 || paddr > 0xffffffffu - (size - 1u))
        return (void *)0;
    offset = paddr & VM_PAGE_MASK;
    physical = paddr & ~(i386_u32)VM_PAGE_MASK;
    if (size > 0xffffffffu - offset - VM_PAGE_MASK)
        return (void *)0;
    span = (size + offset + VM_PAGE_MASK) & ~(i386_u32)VM_PAGE_MASK;
    first = (void *)0;
    for (mapped = 0; mapped < span; mapped += VM_PAGE_SIZE) {
        mapping = pmap_device_direct_map(physical + mapped,
            PMAP_CACHE_UNCACHED);
        if (mapping == (void *)0 ||
            (first != (void *)0 &&
            (unsigned char *)mapping !=
            (unsigned char *)first + mapped))
            return (void *)0;
        if (first == (void *)0)
            first = mapping;
    }
    return (unsigned char *)first + offset;
}

static unsigned
i386_usb_pci_read(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return *(volatile unsigned int *)(controller->up_mmio + reg);
}

static void
i386_usb_pci_write(void *arg, unsigned reg, unsigned value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    *(volatile unsigned int *)(controller->up_mmio + reg) = value;
    __asm__ volatile ("" : : : "memory");
}

static unsigned short
i386_usb_pci_io_read_2(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return i386_inw((i386_u16)(controller->up_io_base + reg));
}

static void
i386_usb_pci_io_write_2(void *arg, unsigned reg, unsigned short value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    i386_outw((i386_u16)(controller->up_io_base + reg), value);
}

static unsigned
i386_usb_pci_io_read_4(void *arg, unsigned reg)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    return i386_inl((i386_u16)(controller->up_io_base + reg));
}

static void
i386_usb_pci_io_write_4(void *arg, unsigned reg, unsigned value)
{
    struct i386_usb_pci_controller *controller;

    controller = (struct i386_usb_pci_controller *)arg;
    i386_outl((i386_u16)(controller->up_io_base + reg), value);
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
    struct i386_pci_function function;
    i386_u32 bar;
    i386_u32 command;
    i386_u32 interrupt;
    void *mapping;

    if (controller->up_prepared)
        return 0;
    if (!i386_pci_find_class(I386_PCI_CLASS_SERIAL_BUS,
        I386_PCI_SUBCLASS_USB, programming_interface, &function))
        return ENXIO;
    controller->up_present = 1;

    bar = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_BAR0);
    if ((bar & I386_PCI_BAR_IO) != 0 ||
        (bar & I386_PCI_BAR_MEMORY_TYPE) != I386_PCI_BAR_MEMORY_32 ||
        (bar & I386_PCI_BAR_MEMORY_ADDRESS) == 0)
        return EINVAL;
    interrupt = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_INTERRUPT_LINE);
    interrupt &= 0xffu;
    if (interrupt == 0 || interrupt >= I386_IRQ_COUNT)
        return EINVAL;

    controller->up_mmio_phys = bar & I386_PCI_BAR_MEMORY_ADDRESS;
    mapping = i386_usb_pci_map_mmio(controller->up_mmio_phys,
        I386_USB_HC_MMIO_BYTES);
    if (mapping == (void *)0)
        return ENOMEM;

    command = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS);
    command &= 0xffffu;
    command |= I386_PCI_COMMAND_MEMORY | I386_PCI_COMMAND_MASTER;
    i386_pci_config_write32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS, command);
    command = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS);
    if ((command & (I386_PCI_COMMAND_MEMORY |
        I386_PCI_COMMAND_MASTER)) !=
        (I386_PCI_COMMAND_MEMORY | I386_PCI_COMMAND_MASTER))
        return EIO;

    controller->up_function = function;
    controller->up_name = name;
    controller->up_mmio = mapping;
    controller->up_irq = interrupt;
    controller->up_prepared = 1;
    i386_early_puts(name);
    i386_early_puts(": pci-id=");
    i386_early_put_hex32(((i386_u32)function.vendor << 16) |
        function.product);
    i386_early_puts(" mmio=");
    i386_early_put_hex32(controller->up_mmio_phys);
    i386_early_puts(" irq=");
    i386_early_put_hex32(controller->up_irq);
    i386_early_putc('\n');
    return 0;
}

static int
i386_usb_pci_prepare_uhci(void)
{
    struct i386_pci_function function;
    i386_u32 bar;
    i386_u32 command;
    i386_u32 interrupt;
    i386_u32 legacy;
    i386_u32 revision;
    i386_u32 io_base;

    if (i386_uhci_pci.up_prepared)
        return 0;
    if (!i386_pci_find_class(I386_PCI_CLASS_SERIAL_BUS,
        I386_PCI_SUBCLASS_USB, I386_PCI_INTERFACE_UHCI, &function))
        return ENXIO;
    i386_uhci_pci.up_present = 1;
    bar = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_BAR4);
    io_base = bar & I386_PCI_BAR_IO_ADDRESS;
    if ((bar & I386_PCI_BAR_IO) == 0 || io_base == 0 ||
        io_base > 0xffe0u)
        return EINVAL;
    interrupt = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_INTERRUPT_LINE) & 0xffu;
    if (interrupt == 0 || interrupt >= I386_IRQ_COUNT)
        return EINVAL;

    command = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS) & 0xffffu;
    command |= I386_PCI_COMMAND_IO | I386_PCI_COMMAND_MASTER;
    i386_pci_config_write32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS, command);
    command = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_COMMAND_STATUS);
    if ((command & (I386_PCI_COMMAND_IO | I386_PCI_COMMAND_MASTER)) !=
        (I386_PCI_COMMAND_IO | I386_PCI_COMMAND_MASTER))
        return EIO;

    legacy = I386_PCI_UHCI_PIRQ_ENABLE;
    i386_pci_config_write32(function.bus, function.device,
        function.function, I386_PCI_UHCI_LEGACY, legacy);
    revision = i386_pci_config_read32(function.bus, function.device,
        function.function, I386_PCI_USB_REVISION);

    i386_uhci_pci.up_function = function;
    i386_uhci_pci.up_name = "uhci0";
    i386_uhci_pci.up_io_base = (i386_u16)io_base;
    i386_uhci_pci.up_revision = (i386_u8)(revision & 0xffu);
    i386_uhci_pci.up_irq = interrupt;
    i386_uhci_pci.up_prepared = 1;
    i386_usb_pci_io_write_2(&i386_uhci_pci, UHCI_INTR, 0);
    i386_usb_pci_io_write_2(&i386_uhci_pci, UHCI_STS,
        i386_usb_pci_io_read_2(&i386_uhci_pci, UHCI_STS) &
        UHCI_STS_ACK);

    i386_early_puts("uhci0: pci-id=");
    i386_early_put_hex32(((i386_u32)function.vendor << 16) |
        function.product);
    i386_early_puts(" io=");
    i386_early_put_hex32(io_base);
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
        "ohci0", I386_PCI_INTERFACE_OHCI);
    if (ohci_error != 0 && ohci_error != ENXIO)
        return ohci_error;
    ehci_error = i386_usb_pci_prepare_mmio_controller(&i386_ehci_pci,
        "ehci0", I386_PCI_INTERFACE_EHCI);
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
    if (!i386_irq_establish(i386_uhci_pci.up_irq,
        i386_uhci_interrupt, &i386_uhci_pci)) {
        usb_root_hub_stop(&i386_uhci_root_hub);
        usb_bus_stop(&i386_uhci_bus);
        return ENOMEM;
    }
    i386_pic_unmask(i386_uhci_pci.up_irq);
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
    if (!i386_irq_establish(i386_ehci_pci.up_irq,
        i386_ehci_interrupt, &i386_ehci_pci)) {
        usb_root_hub_stop(&i386_ehci_root_hub);
        usb_bus_stop(&i386_ehci_bus);
        return ENOMEM;
    }
    i386_pic_unmask(i386_ehci_pci.up_irq);
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
    if (!i386_irq_establish(i386_ohci_pci.up_irq,
        i386_ohci_interrupt, &i386_ohci_pci)) {
        usb_root_hub_stop(&i386_ohci_root_hub);
        usb_bus_stop(&i386_ohci_bus);
        return ENOMEM;
    }
    i386_pic_unmask(i386_ohci_pci.up_irq);
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
