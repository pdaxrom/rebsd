/*
 * i686 machine-dependent attachment for the common PCI bus.
 */

#include "boot.h"
#include "interrupt.h"
#include "io.h"
#include "pci.h"

#include <sys/errno.h>
#include <sys/systm.h>
#include <vm/pmap.h>

#define PCI_CONFIG_ADDRESS       0x0cf8u
#define PCI_CONFIG_DATA          0x0cfcu
#define PCI_CONFIG_ENABLE        0x80000000u

#define PCI_VENDOR_INTEL         0x8086u
#define PCI_VENDOR_VIA           0x1106u

struct i386_pci_inventory {
    struct pci_device host;
    struct pci_device isa;
    struct pci_device ide;
    struct pci_device vga;
    int have_host;
    int have_isa;
    int have_ide;
    int have_vga;
};

static struct pci_bus i386_pci;
static struct i386_pci_inventory i386_pci_last_inventory;
static int i386_pci_inventory_valid;

static i386_u32
i386_pci_config_address(unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset)
{
    return PCI_CONFIG_ENABLE |
        ((i386_u32)bus << 16) |
        ((i386_u32)(device & 0x1fu) << 11) |
        ((i386_u32)(function & 0x07u) << 8) |
        ((i386_u32)offset & 0xfcu);
}

static unsigned char
i386_pci_config_read8(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset)
{
    (void)cookie;
    i386_outl(PCI_CONFIG_ADDRESS,
        i386_pci_config_address(bus, device, function, offset));
    return i386_inb((i386_u16)(PCI_CONFIG_DATA + (offset & 3u)));
}

static unsigned short
i386_pci_config_read16(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset)
{
    (void)cookie;
    i386_outl(PCI_CONFIG_ADDRESS,
        i386_pci_config_address(bus, device, function, offset));
    return i386_inw((i386_u16)(PCI_CONFIG_DATA + (offset & 2u)));
}

static unsigned
i386_pci_config_read32(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset)
{
    i386_u32 address;

    (void)cookie;
    address = i386_pci_config_address(bus, device, function, offset);
    i386_outl(PCI_CONFIG_ADDRESS, address);
    return i386_inl(PCI_CONFIG_DATA);
}

static void
i386_pci_config_write8(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset,
    unsigned char value)
{
    (void)cookie;
    i386_outl(PCI_CONFIG_ADDRESS,
        i386_pci_config_address(bus, device, function, offset));
    i386_outb((i386_u16)(PCI_CONFIG_DATA + (offset & 3u)), value);
}

static void
i386_pci_config_write16(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset,
    unsigned short value)
{
    (void)cookie;
    i386_outl(PCI_CONFIG_ADDRESS,
        i386_pci_config_address(bus, device, function, offset));
    i386_outw((i386_u16)(PCI_CONFIG_DATA + (offset & 2u)), value);
}

static void
i386_pci_config_write32(void *cookie, unsigned char bus,
    unsigned char device, unsigned char function, unsigned char offset,
    unsigned value)
{
    i386_u32 address;

    (void)cookie;
    address = i386_pci_config_address(bus, device, function, offset);
    i386_outl(PCI_CONFIG_ADDRESS, address);
    i386_outl(PCI_CONFIG_DATA, value);
}

static int
i386_pci_mechanism_present(void *cookie)
{
    i386_u32 saved;
    i386_u32 value;

    (void)cookie;
    saved = i386_inl(PCI_CONFIG_ADDRESS);
    i386_outl(PCI_CONFIG_ADDRESS, PCI_CONFIG_ENABLE);
    value = i386_inl(PCI_CONFIG_ADDRESS);
    i386_outl(PCI_CONFIG_ADDRESS, saved);
    return value == PCI_CONFIG_ENABLE;
}

static void *
i386_pci_map_mmio(i386_u32 paddr, size_t size)
{
    void *first;
    void *mapping;
    i386_u32 offset;
    i386_u32 physical;
    i386_u32 span;
    i386_u32 mapped;

    if (size == 0 ||
        paddr > 0xffffffffu - ((i386_u32)size - 1u))
        return 0;
    offset = paddr & VM_PAGE_MASK;
    physical = paddr & ~(i386_u32)VM_PAGE_MASK;
    if ((i386_u32)size > 0xffffffffu - offset - VM_PAGE_MASK)
        return 0;
    span = ((i386_u32)size + offset + VM_PAGE_MASK) &
        ~(i386_u32)VM_PAGE_MASK;
    first = 0;
    for (mapped = 0; mapped < span; mapped += VM_PAGE_SIZE) {
        mapping = pmap_device_direct_map(physical + mapped,
            PMAP_CACHE_UNCACHED);
        if (mapping == 0 ||
            (first != 0 && (unsigned char *)mapping !=
            (unsigned char *)first + mapped))
            return 0;
        if (first == 0)
            first = mapping;
    }
    return (unsigned char *)first + offset;
}

static int
i386_pci_map_resource(void *cookie, enum pci_resource_type type,
    unsigned long long address, size_t size, u_long *handle)
{
    void *mapping;

    (void)cookie;
    if (handle == 0 || size == 0)
        return EINVAL;
    if (type == PCI_RESOURCE_IO) {
        if (address > 0xffffu || size - 1u > 0xffffu - (size_t)address)
            return EOVERFLOW;
        *handle = (u_long)address;
        return 0;
    }
    if (type != PCI_RESOURCE_MEMORY || address > 0xffffffffu)
        return EOPNOTSUPP;
    mapping = i386_pci_map_mmio((i386_u32)address, size);
    if (mapping == 0)
        return ENOMEM;
    *handle = (u_long)mapping;
    return 0;
}

static unsigned char
i386_pci_resource_read8(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        return i386_inb((i386_u16)(handle + offset));
    return *(volatile unsigned char *)(handle + offset);
}

static unsigned short
i386_pci_resource_read16(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        return i386_inw((i386_u16)(handle + offset));
    return *(volatile unsigned short *)(handle + offset);
}

static unsigned
i386_pci_resource_read32(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        return i386_inl((i386_u16)(handle + offset));
    return *(volatile unsigned *)(handle + offset);
}

static void
i386_pci_resource_write8(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned char value)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        i386_outb((i386_u16)(handle + offset), value);
    else
        *(volatile unsigned char *)(handle + offset) = value;
    __asm__ volatile ("" : : : "memory");
}

static void
i386_pci_resource_write16(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned short value)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        i386_outw((i386_u16)(handle + offset), value);
    else
        *(volatile unsigned short *)(handle + offset) = value;
    __asm__ volatile ("" : : : "memory");
}

static void
i386_pci_resource_write32(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned value)
{
    (void)cookie;
    if (type == PCI_RESOURCE_IO)
        i386_outl((i386_u16)(handle + offset), value);
    else
        *(volatile unsigned *)(handle + offset) = value;
    __asm__ volatile ("" : : : "memory");
}

static int
i386_pci_interrupt_establish(void *cookie, unsigned interrupt,
    pci_interrupt_handler_t handler, void *arg)
{
    (void)cookie;
    if (interrupt == 0 || interrupt >= I386_IRQ_COUNT)
        return EINVAL;
    if (!i386_pic_set_level(interrupt))
        return EINVAL;
    if (!i386_irq_establish(interrupt, handler, arg))
        return ENOMEM;
    i386_pic_unmask(interrupt);
    return 0;
}

static void
i386_pci_delay_us(void *cookie, unsigned microseconds)
{
    (void)cookie;
    while (microseconds-- != 0)
        i386_io_wait();
}

static const struct pci_bus_ops i386_pci_ops = {
    i386_pci_mechanism_present,
    i386_pci_config_read8,
    i386_pci_config_read16,
    i386_pci_config_read32,
    i386_pci_config_write8,
    i386_pci_config_write16,
    i386_pci_config_write32,
    i386_pci_map_resource,
    i386_pci_resource_read8,
    i386_pci_resource_read16,
    i386_pci_resource_read32,
    i386_pci_resource_write8,
    i386_pci_resource_write16,
    i386_pci_resource_write32,
    i386_pci_interrupt_establish,
    i386_pci_delay_us,
};

struct pci_bus *
i386_pci_bus(void)
{
    return i386_pci.pb_attached ? &i386_pci : 0;
}

static void
i386_pci_print_id(const char *label, const struct pci_device *function)
{
    i386_early_puts(label);
    i386_early_put_hex32(((i386_u32)function->pd_vendor << 16) |
        function->pd_product);
    i386_early_putc('\n');
}

static void
i386_pci_print_platform(const char *label,
    const struct i386_pci_inventory *inventory)
{
    i386_early_puts(label);
    if (inventory->host.pd_vendor == PCI_VENDOR_VIA ||
        inventory->isa.pd_vendor == PCI_VENDOR_VIA ||
        inventory->ide.pd_vendor == PCI_VENDOR_VIA)
        i386_early_puts("via\n");
    else if (inventory->host.pd_vendor == PCI_VENDOR_INTEL)
        i386_early_puts("intel\n");
    else
        i386_early_puts("generic\n");
}

void
i386_pci_report_summary(void)
{
    const struct i386_pci_inventory *inventory;

    if (!i386_pci_inventory_valid)
        return;
    inventory = &i386_pci_last_inventory;
    i386_early_puts("hardware-summary: pci\n");
    i386_pci_print_id("hardware-pci-host: ", &inventory->host);
    i386_pci_print_id("hardware-pci-isa: ", &inventory->isa);
    i386_pci_print_id("hardware-pci-ide: ", &inventory->ide);
    if (inventory->have_vga)
        i386_pci_print_id("hardware-pci-vga: ", &inventory->vga);
    i386_pci_print_platform("hardware-pci-platform: ", inventory);
}

int
i386_pci_probe(void)
{
    struct i386_pci_inventory inventory;
    int error;

    i386_pci_inventory_valid = 0;
    bzero(&inventory, sizeof(inventory));
    error = pci_bus_scan(&i386_pci, &i386_pci_ops, 0);
    if (error != 0)
        return error;

    inventory.have_host = pci_find_class(&i386_pci, PCI_CLASS_BRIDGE,
        PCI_SUBCLASS_HOST, PCI_INTERFACE_ANY, &inventory.host);
    inventory.have_isa = pci_find_class(&i386_pci, PCI_CLASS_BRIDGE,
        PCI_SUBCLASS_ISA, PCI_INTERFACE_ANY, &inventory.isa);
    inventory.have_ide = pci_find_class(&i386_pci,
        PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_IDE, PCI_INTERFACE_ANY,
        &inventory.ide);
    inventory.have_vga = pci_find_class(&i386_pci, PCI_CLASS_DISPLAY,
        PCI_SUBCLASS_VGA, PCI_INTERFACE_ANY, &inventory.vga);
    i386_pci_last_inventory = inventory;
    i386_pci_inventory_valid = 1;

    if (!inventory.have_host || !inventory.have_isa || !inventory.have_ide)
        return ENXIO;
    i386_early_puts("pci: mechanism=1\n");
    i386_early_puts("pci-functions: ");
    i386_early_put_hex32(pci_bus_function_count(&i386_pci));
    i386_early_putc('\n');
    i386_pci_print_id("pci-host: ", &inventory.host);
    i386_pci_print_id("pci-isa: ", &inventory.isa);
    i386_pci_print_id("pci-ide: ", &inventory.ide);
    if (inventory.have_vga)
        i386_pci_print_id("pci-vga: ", &inventory.vga);
    i386_pci_print_platform("pci-platform: ", &inventory);
    return 0;
}
