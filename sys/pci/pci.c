/*
 * Machine-independent PCI enumeration and resource access.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/hw_inventory_provider.h>

#include <pci/pci.h>

static struct pci_bus *pci_primary;

static struct kinfo_pci_inventory *
pci_inventory_snapshot(void)
{
    if (pci_primary == 0 || !pci_primary->pb_attached)
        return 0;
    return &pci_primary->pb_inventory;
}

static int
pci_bus_ops_valid(const struct pci_bus_ops *ops)
{
    return ops != 0 && ops->pbo_present != 0 &&
        ops->pbo_config_read8 != 0 && ops->pbo_config_read16 != 0 &&
        ops->pbo_config_read32 != 0 && ops->pbo_config_write32 != 0 &&
        ops->pbo_config_write8 != 0 && ops->pbo_config_write16 != 0 &&
        ops->pbo_map_resource != 0 &&
        ops->pbo_resource_read8 != 0 &&
        ops->pbo_resource_read16 != 0 &&
        ops->pbo_resource_read32 != 0 &&
        ops->pbo_resource_write8 != 0 &&
        ops->pbo_resource_write16 != 0 &&
        ops->pbo_resource_write32 != 0 &&
        ops->pbo_interrupt_establish != 0 && ops->pbo_delay_us != 0;
}

static unsigned
pci_bus_config_read32(const struct pci_bus *bus, unsigned char bus_number,
    unsigned char device, unsigned char function, unsigned char offset)
{
    return bus->pb_ops->pbo_config_read32(bus->pb_cookie, bus_number,
        device, function, offset & 0xfcu);
}

static void
pci_bus_config_write32(const struct pci_bus *bus, unsigned char bus_number,
    unsigned char device, unsigned char function, unsigned char offset,
    unsigned value)
{
    bus->pb_ops->pbo_config_write32(bus->pb_cookie, bus_number, device,
        function, offset & 0xfcu, value);
}

static int
pci_scan_function(struct pci_bus *bus, unsigned char bus_number,
    unsigned char device_number, unsigned char function_number)
{
    struct kinfo_pci_device *device;
    unsigned class_revision;
    unsigned identity;

    identity = pci_bus_config_read32(bus, bus_number, device_number,
        function_number, 0);
    if ((identity & 0xffffu) == PCI_VENDOR_INVALID ||
        (identity & 0xffffu) == 0)
        return 0;

    ++bus->pb_function_count;
    if (bus->pb_inventory.kpi_count >= KINFO_PCI_MAXDEVICES) {
        bus->pb_inventory.kpi_truncated = 1;
        return 1;
    }

    class_revision = pci_bus_config_read32(bus, bus_number, device_number,
        function_number, PCI_CONFIG_CLASS_REVISION);
    device = &bus->pb_inventory.kpi_devices[
        bus->pb_inventory.kpi_count++];
    device->kpd_bus = bus_number;
    device->kpd_device = device_number;
    device->kpd_function = function_number;
    device->kpd_class = (unsigned char)(class_revision >> 24);
    device->kpd_subclass = (unsigned char)(class_revision >> 16);
    device->kpd_programming_interface =
        (unsigned char)(class_revision >> 8);
    device->kpd_revision = (unsigned char)class_revision;
    device->kpd_vendor = (unsigned short)identity;
    device->kpd_product = (unsigned short)(identity >> 16);
    return 1;
}

int
pci_bus_scan(struct pci_bus *bus, const struct pci_bus_ops *ops, void *cookie)
{
    unsigned bus_number;
    unsigned device_number;
    unsigned function_count;
    unsigned function_number;
    unsigned header;

    if (bus == 0 || !pci_bus_ops_valid(ops))
        return EINVAL;
    if (pci_primary == bus)
        pci_primary = 0;
    bzero(bus, sizeof(*bus));
    bus->pb_ops = ops;
    bus->pb_cookie = cookie;
    if (!ops->pbo_present(cookie))
        return ENXIO;

    for (bus_number = 0; bus_number < 256u; ++bus_number) {
        for (device_number = 0; device_number < 32u; ++device_number) {
            if (!pci_scan_function(bus, (unsigned char)bus_number,
                (unsigned char)device_number, 0))
                continue;
            header = pci_bus_config_read32(bus,
                (unsigned char)bus_number, (unsigned char)device_number,
                0, PCI_CONFIG_HEADER);
            function_count =
                ((header >> 16) & PCI_HEADER_MULTIFUNCTION) != 0 ?
                8u : 1u;
            for (function_number = 1;
                function_number < function_count; ++function_number)
                (void)pci_scan_function(bus, (unsigned char)bus_number,
                    (unsigned char)device_number,
                    (unsigned char)function_number);
        }
    }

    bus->pb_attached = 1;
    if (pci_primary == 0)
        pci_primary = bus;
    hw_inventory_register_pci(pci_inventory_snapshot);
    return 0;
}

struct pci_bus *
pci_primary_bus(void)
{
    if (pci_primary == 0 || !pci_primary->pb_attached)
        return 0;
    return pci_primary;
}

unsigned
pci_bus_function_count(const struct pci_bus *bus)
{
    return bus != 0 && bus->pb_attached ? bus->pb_function_count : 0;
}

int
pci_device_at(struct pci_bus *bus, unsigned index, struct pci_device *result)
{
    const struct kinfo_pci_device *source;

    if (bus == 0 || result == 0 || !bus->pb_attached ||
        index >= bus->pb_inventory.kpi_count)
        return 0;
    source = &bus->pb_inventory.kpi_devices[index];
    result->pd_bus = bus;
    result->pd_bus_number = source->kpd_bus;
    result->pd_device = source->kpd_device;
    result->pd_function = source->kpd_function;
    result->pd_class = source->kpd_class;
    result->pd_subclass = source->kpd_subclass;
    result->pd_interface = source->kpd_programming_interface;
    result->pd_revision = source->kpd_revision;
    result->pd_vendor = source->kpd_vendor;
    result->pd_product = source->kpd_product;
    return 1;
}

int
pci_find_class(struct pci_bus *bus, unsigned char class_code,
    unsigned char subclass, int programming_interface,
    struct pci_device *result)
{
    struct pci_device candidate;
    unsigned index;

    if (bus == 0 || result == 0)
        return 0;
    for (index = 0; pci_device_at(bus, index, &candidate); ++index) {
        if (candidate.pd_class == class_code &&
            candidate.pd_subclass == subclass &&
            (programming_interface == PCI_INTERFACE_ANY ||
            candidate.pd_interface == (unsigned char)programming_interface)) {
            *result = candidate;
            return 1;
        }
    }
    return 0;
}

int
pci_find_device(struct pci_bus *bus, unsigned short vendor,
    unsigned short product, struct pci_device *result)
{
    struct pci_device candidate;
    unsigned index;

    if (bus == 0 || result == 0)
        return 0;
    for (index = 0; pci_device_at(bus, index, &candidate); ++index) {
        if (candidate.pd_vendor == vendor &&
            candidate.pd_product == product) {
            *result = candidate;
            return 1;
        }
    }
    return 0;
}

unsigned
pci_config_read32(const struct pci_device *device, unsigned char offset)
{
    if (device == 0 || device->pd_bus == 0)
        return 0xffffffffu;
    return pci_bus_config_read32(device->pd_bus, device->pd_bus_number,
        device->pd_device, device->pd_function, offset);
}

void
pci_config_write32(const struct pci_device *device, unsigned char offset,
    unsigned value)
{
    if (device == 0 || device->pd_bus == 0)
        return;
    pci_bus_config_write32(device->pd_bus, device->pd_bus_number,
        device->pd_device, device->pd_function, offset, value);
}

unsigned char
pci_config_read8(const struct pci_device *device, unsigned char offset)
{
    if (device == 0 || device->pd_bus == 0)
        return 0xffu;
    return device->pd_bus->pb_ops->pbo_config_read8(
        device->pd_bus->pb_cookie, device->pd_bus_number,
        device->pd_device, device->pd_function, offset);
}

unsigned short
pci_config_read16(const struct pci_device *device, unsigned char offset)
{
    if (device == 0 || device->pd_bus == 0 || (offset & 1u))
        return 0xffffu;
    return device->pd_bus->pb_ops->pbo_config_read16(
        device->pd_bus->pb_cookie, device->pd_bus_number,
        device->pd_device, device->pd_function, offset);
}

void
pci_config_write8(const struct pci_device *device, unsigned char offset,
    unsigned char value)
{
    if (device == 0 || device->pd_bus == 0)
        return;
    device->pd_bus->pb_ops->pbo_config_write8(
        device->pd_bus->pb_cookie, device->pd_bus_number,
        device->pd_device, device->pd_function, offset, value);
}

void
pci_config_write16(const struct pci_device *device, unsigned char offset,
    unsigned short value)
{
    if (device == 0 || device->pd_bus == 0 || (offset & 1u))
        return;
    device->pd_bus->pb_ops->pbo_config_write16(
        device->pd_bus->pb_cookie, device->pd_bus_number,
        device->pd_device, device->pd_function, offset, value);
}

int
pci_device_enable(const struct pci_device *device, unsigned enables)
{
    unsigned command;

    if (device == 0 || (enables & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
        PCI_COMMAND_MASTER)) != 0)
        return EINVAL;
    command = pci_config_read32(device, PCI_CONFIG_COMMAND_STATUS) &
        0xffffu;
    command |= enables;
    pci_config_write32(device, PCI_CONFIG_COMMAND_STATUS, command);
    command = pci_config_read32(device, PCI_CONFIG_COMMAND_STATUS);
    return (command & enables) == enables ? 0 : EIO;
}

static int
pci_bar_probe(const struct pci_device *device, unsigned bar,
    enum pci_resource_type *type, unsigned *prefetchable,
    unsigned long long *address, unsigned long long *size)
{
    unsigned command;
    unsigned high;
    unsigned high_mask;
    unsigned low;
    unsigned low_mask;
    unsigned offset;
    unsigned memory_type;
    unsigned long long mask;

    if (device == 0 || device->pd_bus == 0 || bar >= 6u)
        return EINVAL;
    offset = PCI_CONFIG_BAR(bar);
    low = pci_config_read32(device, (unsigned char)offset);
    if (low == 0 || low == 0xffffffffu)
        return ENXIO;

    high = 0;
    high_mask = 0;
    if (low & PCI_BAR_IO) {
        *type = PCI_RESOURCE_IO;
        *prefetchable = 0;
    } else {
        memory_type = low & PCI_BAR_MEMORY_TYPE;
        if (memory_type != PCI_BAR_MEMORY_32 &&
            memory_type != PCI_BAR_MEMORY_64)
            return EOPNOTSUPP;
        if (memory_type == PCI_BAR_MEMORY_64) {
            if (bar == 5u)
                return EINVAL;
            high = pci_config_read32(device,
                (unsigned char)(offset + 4u));
        }
        *type = PCI_RESOURCE_MEMORY;
        *prefetchable = (low & PCI_BAR_MEMORY_PREFETCHABLE) != 0;
    }

    command = pci_config_read32(device, PCI_CONFIG_COMMAND_STATUS) &
        0xffffu;
    pci_config_write32(device, PCI_CONFIG_COMMAND_STATUS,
        command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
    pci_config_write32(device, (unsigned char)offset, 0xffffffffu);
    if (*type == PCI_RESOURCE_MEMORY &&
        (low & PCI_BAR_MEMORY_TYPE) == PCI_BAR_MEMORY_64)
        pci_config_write32(device, (unsigned char)(offset + 4u),
            0xffffffffu);
    low_mask = pci_config_read32(device, (unsigned char)offset);
    if (*type == PCI_RESOURCE_MEMORY &&
        (low & PCI_BAR_MEMORY_TYPE) == PCI_BAR_MEMORY_64) {
        high_mask = pci_config_read32(device,
            (unsigned char)(offset + 4u));
        pci_config_write32(device, (unsigned char)(offset + 4u), high);
    }
    pci_config_write32(device, (unsigned char)offset, low);
    pci_config_write32(device, PCI_CONFIG_COMMAND_STATUS, command);

    if (*type == PCI_RESOURCE_IO) {
        *address = low & PCI_BAR_IO_ADDRESS;
        mask = low_mask & PCI_BAR_IO_ADDRESS;
        mask |= 0xffffffff00000000ull;
    } else {
        *address = low & PCI_BAR_MEMORY_ADDRESS;
        mask = low_mask & PCI_BAR_MEMORY_ADDRESS;
        if ((low & PCI_BAR_MEMORY_TYPE) == PCI_BAR_MEMORY_64) {
            *address |= (unsigned long long)high << 32;
            mask |= (unsigned long long)high_mask << 32;
        } else {
            mask |= 0xffffffff00000000ull;
        }
    }
    if (*address == 0 || mask == 0)
        return ENXIO;
    *size = (~mask) + 1ull;
    if (*size == 0)
        return EINVAL;
    return 0;
}

int
pci_map_bar(const struct pci_device *device, unsigned bar, size_t minimum_size,
    struct pci_resource *resource)
{
    enum pci_resource_type type;
    unsigned prefetchable;
    unsigned long long address;
    unsigned long long size;
    u_long handle;
    int error;

    if (resource == 0 || minimum_size == 0)
        return EINVAL;
    error = pci_bar_probe(device, bar, &type, &prefetchable, &address,
        &size);
    if (error != 0)
        return error;
    if (size < minimum_size ||
        size > (unsigned long long)(size_t)~(size_t)0)
        return EINVAL;
    error = device->pd_bus->pb_ops->pbo_map_resource(
        device->pd_bus->pb_cookie, type, address, (size_t)size, &handle);
    if (error != 0)
        return error;
    resource->pr_bus = device->pd_bus;
    resource->pr_type = type;
    resource->pr_bar = bar;
    resource->pr_prefetchable = prefetchable;
    resource->pr_address = address;
    resource->pr_size = (size_t)size;
    resource->pr_handle = handle;
    return 0;
}

int
pci_map_fixed_resource(const struct pci_device *device,
    enum pci_resource_type type, unsigned long long address, size_t size,
    struct pci_resource *resource)
{
    u_long handle;
    int error;

    if (device == 0 || device->pd_bus == 0 || resource == 0 || size == 0 ||
        (type != PCI_RESOURCE_IO && type != PCI_RESOURCE_MEMORY))
        return EINVAL;
    error = device->pd_bus->pb_ops->pbo_map_resource(
        device->pd_bus->pb_cookie, type, address, size, &handle);
    if (error != 0)
        return error;
    resource->pr_bus = device->pd_bus;
    resource->pr_type = type;
    resource->pr_bar = 0xffffffffu;
    resource->pr_prefetchable = 0;
    resource->pr_address = address;
    resource->pr_size = size;
    resource->pr_handle = handle;
    return 0;
}

int
pci_interrupt_establish(const struct pci_device *device,
    pci_interrupt_handler_t handler, void *arg)
{
    unsigned interrupt;

    if (device == 0 || device->pd_bus == 0 || handler == 0)
        return EINVAL;
    interrupt = pci_config_read32(device, PCI_CONFIG_INTERRUPT) & 0xffu;
    if (interrupt == 0xffu)
        return ENXIO;
    return device->pd_bus->pb_ops->pbo_interrupt_establish(
        device->pd_bus->pb_cookie, interrupt, handler, arg);
}

void
pci_delay_us(const struct pci_device *device, unsigned microseconds)
{
    if (device == 0 || device->pd_bus == 0)
        return;
    device->pd_bus->pb_ops->pbo_delay_us(device->pd_bus->pb_cookie,
        microseconds);
}

unsigned char
pci_resource_read8(const struct pci_resource *resource, size_t offset)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset >= resource->pr_size)
        return 0xffu;
    return resource->pr_bus->pb_ops->pbo_resource_read8(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset);
}

unsigned short
pci_resource_read16(const struct pci_resource *resource, size_t offset)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset > resource->pr_size || 2u > resource->pr_size - offset)
        return 0xffffu;
    return resource->pr_bus->pb_ops->pbo_resource_read16(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset);
}

unsigned
pci_resource_read32(const struct pci_resource *resource, size_t offset)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset > resource->pr_size || 4u > resource->pr_size - offset)
        return 0xffffffffu;
    return resource->pr_bus->pb_ops->pbo_resource_read32(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset);
}

void
pci_resource_write8(const struct pci_resource *resource, size_t offset,
    unsigned char value)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset >= resource->pr_size)
        return;
    resource->pr_bus->pb_ops->pbo_resource_write8(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset, value);
}

void
pci_resource_write16(const struct pci_resource *resource, size_t offset,
    unsigned short value)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset > resource->pr_size || 2u > resource->pr_size - offset)
        return;
    resource->pr_bus->pb_ops->pbo_resource_write16(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset, value);
}

void
pci_resource_write32(const struct pci_resource *resource, size_t offset,
    unsigned value)
{
    if (resource == 0 || resource->pr_bus == 0 ||
        offset > resource->pr_size || 4u > resource->pr_size - offset)
        return;
    resource->pr_bus->pb_ops->pbo_resource_write32(
        resource->pr_bus->pb_cookie, resource->pr_type,
        resource->pr_handle, offset, value);
}
