#ifndef _I386_PCI_H_
#define _I386_PCI_H_

#include "boot.h"

#define I386_PCI_VENDOR_INVALID 0xffffu

#define I386_PCI_CLASS_SERIAL_BUS       0x0cu
#define I386_PCI_SUBCLASS_USB           0x03u
#define I386_PCI_INTERFACE_OHCI         0x10u
#define I386_PCI_INTERFACE_EHCI         0x20u

#define I386_PCI_COMMAND_MEMORY         0x0002u
#define I386_PCI_COMMAND_MASTER         0x0004u

struct i386_pci_function {
    i386_u8 bus;
    i386_u8 device;
    i386_u8 function;
    i386_u8 class_code;
    i386_u8 subclass;
    i386_u8 programming_interface;
    i386_u16 vendor;
    i386_u16 product;
};

i386_u32 i386_pci_config_read32(i386_u8 bus, i386_u8 device,
    i386_u8 function, i386_u8 offset);
void i386_pci_config_write32(i386_u8 bus, i386_u8 device,
    i386_u8 function, i386_u8 offset, i386_u32 value);
int i386_pci_find_class(i386_u8 class_code, i386_u8 subclass,
    i386_u8 programming_interface, struct i386_pci_function *);
int i386_pci_probe(void);
void i386_pci_report_summary(void);

#endif
