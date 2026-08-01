/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _PCI_PCI_H_
#define _PCI_PCI_H_

#include <sys/types.h>
#include <sys/hw_inventory.h>

#define PCI_VENDOR_INVALID              0xffffu

#define PCI_CLASS_MASS_STORAGE          0x01u
#define PCI_SUBCLASS_IDE                0x01u
#define PCI_CLASS_NETWORK               0x02u
#define PCI_SUBCLASS_ETHERNET           0x00u
#define PCI_CLASS_DISPLAY               0x03u
#define PCI_SUBCLASS_VGA                0x00u
#define PCI_CLASS_BRIDGE                0x06u
#define PCI_SUBCLASS_HOST               0x00u
#define PCI_SUBCLASS_ISA                0x01u
#define PCI_CLASS_SERIAL_BUS            0x0cu
#define PCI_SUBCLASS_USB                0x03u
#define PCI_INTERFACE_UHCI              0x00u
#define PCI_INTERFACE_OHCI              0x10u
#define PCI_INTERFACE_EHCI              0x20u
#define PCI_INTERFACE_ANY               (-1)

#define PCI_COMMAND_IO                  0x0001u
#define PCI_COMMAND_MEMORY              0x0002u
#define PCI_COMMAND_MASTER              0x0004u

#define PCI_CONFIG_COMMAND_STATUS       0x04u
#define PCI_CONFIG_CLASS_REVISION       0x08u
#define PCI_CONFIG_HEADER               0x0cu
#define PCI_CONFIG_BAR(index)           (0x10u + (index) * 4u)
#define PCI_CONFIG_INTERRUPT            0x3cu

#define PCI_HEADER_MULTIFUNCTION        0x80u
#define PCI_BAR_IO                      0x01u
#define PCI_BAR_IO_ADDRESS              0xfffffffcu
#define PCI_BAR_MEMORY_TYPE             0x06u
#define PCI_BAR_MEMORY_32               0x00u
#define PCI_BAR_MEMORY_64               0x04u
#define PCI_BAR_MEMORY_PREFETCHABLE     0x08u
#define PCI_BAR_MEMORY_ADDRESS          0xfffffff0u

enum pci_resource_type {
    PCI_RESOURCE_IO = 1,
    PCI_RESOURCE_MEMORY
};

struct pci_device;
struct pci_resource;

typedef int (*pci_interrupt_handler_t)(void *);

struct pci_bus_ops {
    int (*pbo_present)(void *);
    unsigned char (*pbo_config_read8)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char);
    unsigned short (*pbo_config_read16)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char);
    unsigned (*pbo_config_read32)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char);
    void (*pbo_config_write8)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char, unsigned char);
    void (*pbo_config_write16)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char, unsigned short);
    void (*pbo_config_write32)(void *, unsigned char, unsigned char,
        unsigned char, unsigned char, unsigned);
    int (*pbo_map_resource)(void *, enum pci_resource_type,
        unsigned long long, size_t, u_long *);
    unsigned char (*pbo_resource_read8)(void *, enum pci_resource_type,
        u_long, size_t);
    unsigned short (*pbo_resource_read16)(void *, enum pci_resource_type,
        u_long, size_t);
    unsigned (*pbo_resource_read32)(void *, enum pci_resource_type,
        u_long, size_t);
    void (*pbo_resource_write8)(void *, enum pci_resource_type,
        u_long, size_t, unsigned char);
    void (*pbo_resource_write16)(void *, enum pci_resource_type,
        u_long, size_t, unsigned short);
    void (*pbo_resource_write32)(void *, enum pci_resource_type,
        u_long, size_t, unsigned);
    int (*pbo_interrupt_establish)(void *, unsigned,
        pci_interrupt_handler_t, void *);
    void (*pbo_delay_us)(void *, unsigned);
};

struct pci_bus {
    const struct pci_bus_ops *pb_ops;
    void *pb_cookie;
    struct kinfo_pci_inventory pb_inventory;
    unsigned pb_function_count;
    unsigned pb_attached;
};

struct pci_device {
    struct pci_bus *pd_bus;
    unsigned char pd_bus_number;
    unsigned char pd_device;
    unsigned char pd_function;
    unsigned char pd_class;
    unsigned char pd_subclass;
    unsigned char pd_interface;
    unsigned char pd_revision;
    unsigned short pd_vendor;
    unsigned short pd_product;
};

struct pci_resource {
    struct pci_bus *pr_bus;
    enum pci_resource_type pr_type;
    unsigned pr_bar;
    unsigned pr_prefetchable;
    unsigned long long pr_address;
    size_t pr_size;
    u_long pr_handle;
};

int pci_bus_scan(struct pci_bus *, const struct pci_bus_ops *, void *);
struct pci_bus *pci_primary_bus(void);
unsigned pci_bus_function_count(const struct pci_bus *);
int pci_device_at(struct pci_bus *, unsigned, struct pci_device *);
int pci_find_class(struct pci_bus *, unsigned char, unsigned char, int,
    struct pci_device *);
int pci_find_device(struct pci_bus *, unsigned short, unsigned short,
    struct pci_device *);

unsigned pci_config_read32(const struct pci_device *, unsigned char);
void pci_config_write32(const struct pci_device *, unsigned char, unsigned);
unsigned char pci_config_read8(const struct pci_device *, unsigned char);
unsigned short pci_config_read16(const struct pci_device *, unsigned char);
void pci_config_write8(const struct pci_device *, unsigned char,
    unsigned char);
void pci_config_write16(const struct pci_device *, unsigned char,
    unsigned short);
int pci_device_enable(const struct pci_device *, unsigned);
int pci_map_bar(const struct pci_device *, unsigned, size_t,
    struct pci_resource *);
int pci_map_fixed_resource(const struct pci_device *,
    enum pci_resource_type, unsigned long long, size_t,
    struct pci_resource *);
int pci_interrupt_establish(const struct pci_device *,
    pci_interrupt_handler_t, void *);
void pci_delay_us(const struct pci_device *, unsigned);

unsigned char pci_resource_read8(const struct pci_resource *, size_t);
unsigned short pci_resource_read16(const struct pci_resource *, size_t);
unsigned pci_resource_read32(const struct pci_resource *, size_t);
void pci_resource_write8(const struct pci_resource *, size_t, unsigned char);
void pci_resource_write16(const struct pci_resource *, size_t,
    unsigned short);
void pci_resource_write32(const struct pci_resource *, size_t, unsigned);

#endif /* _PCI_PCI_H_ */
