#ifndef _I386_PCI_H_
#define _I386_PCI_H_

#include "boot.h"

#define I386_PCI_VENDOR_INVALID 0xffffu

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
int i386_pci_probe(void);

#endif
