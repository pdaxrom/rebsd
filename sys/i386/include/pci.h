#ifndef _I386_PCI_H_
#define _I386_PCI_H_

#include <pci/pci.h>

int i386_pci_probe(void);
void i386_pci_report_summary(void);
struct pci_bus *i386_pci_bus(void);

#endif
