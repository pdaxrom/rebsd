#include "boot.h"
#include "io.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS       0x0cf8u
#define PCI_CONFIG_DATA          0x0cfcu
#define PCI_CONFIG_ENABLE        0x80000000u

#define PCI_CLASS_MASS_STORAGE   0x01u
#define PCI_SUBCLASS_IDE         0x01u
#define PCI_CLASS_BRIDGE         0x06u
#define PCI_SUBCLASS_HOST        0x00u
#define PCI_SUBCLASS_ISA         0x01u
#define PCI_CLASS_DISPLAY        0x03u
#define PCI_SUBCLASS_VGA         0x00u

#define PCI_HEADER_MULTIFUNCTION 0x80u

#define PCI_VENDOR_INTEL        0x8086u
#define PCI_VENDOR_VIA          0x1106u

struct i386_pci_inventory {
    struct i386_pci_function host;
    struct i386_pci_function isa;
    struct i386_pci_function ide;
    struct i386_pci_function vga;
    unsigned count;
    int have_host;
    int have_isa;
    int have_ide;
    int have_vga;
};

static struct i386_pci_inventory i386_pci_last_inventory;
static int i386_pci_inventory_valid;

i386_u32
i386_pci_config_read32(i386_u8 bus, i386_u8 device,
    i386_u8 function, i386_u8 offset)
{
    i386_u32 address;

    address = PCI_CONFIG_ENABLE |
        ((i386_u32)bus << 16) |
        ((i386_u32)(device & 0x1fu) << 11) |
        ((i386_u32)(function & 0x07u) << 8) |
        ((i386_u32)offset & 0xfcu);
    i386_outl(PCI_CONFIG_ADDRESS, address);
    return i386_inl(PCI_CONFIG_DATA);
}

void
i386_pci_config_write32(i386_u8 bus, i386_u8 device,
    i386_u8 function, i386_u8 offset, i386_u32 value)
{
    i386_u32 address;

    address = PCI_CONFIG_ENABLE |
        ((i386_u32)bus << 16) |
        ((i386_u32)(device & 0x1fu) << 11) |
        ((i386_u32)(function & 0x07u) << 8) |
        ((i386_u32)offset & 0xfcu);
    i386_outl(PCI_CONFIG_ADDRESS, address);
    i386_outl(PCI_CONFIG_DATA, value);
}

static int
i386_pci_mechanism_present(void)
{
    i386_u32 saved;
    i386_u32 value;

    saved = i386_inl(PCI_CONFIG_ADDRESS);
    i386_outl(PCI_CONFIG_ADDRESS, PCI_CONFIG_ENABLE);
    value = i386_inl(PCI_CONFIG_ADDRESS);
    i386_outl(PCI_CONFIG_ADDRESS, saved);
    return value == PCI_CONFIG_ENABLE;
}

static int
i386_pci_function_read(i386_u8 bus, i386_u8 device,
    i386_u8 function, struct i386_pci_function *result)
{
    i386_u32 class_revision;
    i386_u32 identity;

    identity = i386_pci_config_read32(bus, device, function, 0x00u);
    if ((identity & 0xffffu) == I386_PCI_VENDOR_INVALID ||
        (identity & 0xffffu) == 0)
        return 0;
    class_revision = i386_pci_config_read32(bus, device, function, 0x08u);
    result->bus = bus;
    result->device = device;
    result->function = function;
    result->vendor = (i386_u16)(identity & 0xffffu);
    result->product = (i386_u16)(identity >> 16);
    result->programming_interface =
        (i386_u8)((class_revision >> 8) & 0xffu);
    result->subclass = (i386_u8)((class_revision >> 16) & 0xffu);
    result->class_code = (i386_u8)(class_revision >> 24);
    return 1;
}

typedef int (*i386_pci_visit_t)(const struct i386_pci_function *, void *);

static int
i386_pci_walk(i386_pci_visit_t visit, void *arg)
{
    struct i386_pci_function candidate;
    i386_u32 header;
    unsigned bus;
    unsigned device;
    unsigned function_count;
    unsigned function_index;

    if (visit == (i386_pci_visit_t)0 ||
        !i386_pci_mechanism_present())
        return 0;
    for (bus = 0; bus < 256u; ++bus) {
        for (device = 0; device < 32u; ++device) {
            if (!i386_pci_function_read((i386_u8)bus,
                (i386_u8)device, 0, &candidate))
                continue;
            header = i386_pci_config_read32((i386_u8)bus,
                (i386_u8)device, 0, 0x0cu);
            function_count =
                ((header >> 16) & PCI_HEADER_MULTIFUNCTION) != 0 ?
                8u : 1u;
            for (function_index = 0;
                function_index < function_count; ++function_index) {
                if (function_index != 0 &&
                    !i386_pci_function_read((i386_u8)bus,
                    (i386_u8)device, (i386_u8)function_index,
                    &candidate))
                    continue;
                if (visit(&candidate, arg))
                    return 1;
            }
        }
    }
    return 1;
}

struct i386_pci_class_search {
    i386_u8 class_code;
    i386_u8 subclass;
    i386_u8 programming_interface;
    struct i386_pci_function *result;
    int found;
};

static int
i386_pci_match_class(const struct i386_pci_function *candidate, void *arg)
{
    struct i386_pci_class_search *search;

    search = (struct i386_pci_class_search *)arg;
    if (candidate->class_code != search->class_code ||
        candidate->subclass != search->subclass ||
        candidate->programming_interface !=
        search->programming_interface)
        return 0;
    *search->result = *candidate;
    search->found = 1;
    return 1;
}

int
i386_pci_find_class(i386_u8 class_code, i386_u8 subclass,
    i386_u8 programming_interface, struct i386_pci_function *result)
{
    struct i386_pci_class_search search;

    if (result == (struct i386_pci_function *)0)
        return 0;
    search.class_code = class_code;
    search.subclass = subclass;
    search.programming_interface = programming_interface;
    search.result = result;
    search.found = 0;
    if (!i386_pci_walk(i386_pci_match_class, &search))
        return 0;
    return search.found;
}

static void
i386_pci_inventory_add(struct i386_pci_inventory *inventory,
    const struct i386_pci_function *function)
{
    ++inventory->count;
    if (!inventory->have_host &&
        function->class_code == PCI_CLASS_BRIDGE &&
        function->subclass == PCI_SUBCLASS_HOST) {
        inventory->host = *function;
        inventory->have_host = 1;
    }
    if (!inventory->have_isa &&
        function->class_code == PCI_CLASS_BRIDGE &&
        function->subclass == PCI_SUBCLASS_ISA) {
        inventory->isa = *function;
        inventory->have_isa = 1;
    }
    if (!inventory->have_ide &&
        function->class_code == PCI_CLASS_MASS_STORAGE &&
        function->subclass == PCI_SUBCLASS_IDE) {
        inventory->ide = *function;
        inventory->have_ide = 1;
    }
    if (!inventory->have_vga &&
        function->class_code == PCI_CLASS_DISPLAY &&
        function->subclass == PCI_SUBCLASS_VGA) {
        inventory->vga = *function;
        inventory->have_vga = 1;
    }
}

static int
i386_pci_inventory_visit(const struct i386_pci_function *function,
    void *arg)
{
    i386_pci_inventory_add((struct i386_pci_inventory *)arg, function);
    return 0;
}

static void
i386_pci_print_id(const char *label,
    const struct i386_pci_function *function)
{
    i386_early_puts(label);
    i386_early_put_hex32(((i386_u32)function->vendor << 16) |
        function->product);
    i386_early_putc('\n');
}

static void
i386_pci_print_platform(const char *label,
    const struct i386_pci_inventory *inventory)
{
    i386_early_puts(label);
    if (inventory->host.vendor == PCI_VENDOR_VIA ||
        inventory->isa.vendor == PCI_VENDOR_VIA ||
        inventory->ide.vendor == PCI_VENDOR_VIA)
        i386_early_puts("via\n");
    else if (inventory->host.vendor == PCI_VENDOR_INTEL)
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
    unsigned function_index;

    i386_pci_inventory_valid = 0;
    for (function_index = 0;
        function_index < sizeof(inventory); ++function_index)
        ((i386_u8 *)&inventory)[function_index] = 0;
    if (!i386_pci_walk(i386_pci_inventory_visit, &inventory))
        return 1;

    if (!inventory.have_host || !inventory.have_isa ||
        !inventory.have_ide)
        return 1;
    i386_early_puts("pci: mechanism=1\n");
    i386_early_puts("pci-functions: ");
    i386_early_put_hex32(inventory.count);
    i386_early_putc('\n');
    i386_pci_print_id("pci-host: ", &inventory.host);
    i386_pci_print_id("pci-isa: ", &inventory.isa);
    i386_pci_print_id("pci-ide: ", &inventory.ide);
    if (inventory.have_vga)
        i386_pci_print_id("pci-vga: ", &inventory.vga);
    i386_pci_print_platform("pci-platform: ", &inventory);
    i386_pci_last_inventory = inventory;
    i386_pci_inventory_valid = 1;
    return 0;
}
