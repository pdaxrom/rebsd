/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _PCI_PCIIDE_H_
#define _PCI_PCIIDE_H_

#include <sys/dma.h>

#include <disk/disk.h>
#include <pci/pci.h>

#define PCIIDE_PRIMARY_COMMAND_PORT     0x01f0u
#define PCIIDE_PRIMARY_CONTROL_PORT     0x03f6u
#define PCIIDE_PRIMARY_IRQ              14u
#define PCIIDE_DMA_BUFFER_BYTES         (128u * 1024u)
#define PCIIDE_DMA_MAX_SECTORS          \
    (PCIIDE_DMA_BUFFER_BYTES / DISK_SECTOR_SIZE)

enum pciide_mode_policy {
    PCIIDE_MODE_AUTO = 0,
    PCIIDE_MODE_PIO,
    PCIIDE_MODE_DMA
};

enum pciide_transfer_mode {
    PCIIDE_TRANSFER_PIO = 0,
    PCIIDE_TRANSFER_DMA
};

enum pciide_dma_protocol {
    PCIIDE_DMA_MWDMA = 0,
    PCIIDE_DMA_UDMA
};

typedef int (*pciide_irq_establish_t)(void *, unsigned,
    pci_interrupt_handler_t, void *);
typedef int (*pciide_wait_t)(void *, volatile unsigned *, unsigned);
typedef void (*pciide_wakeup_t)(void *, volatile unsigned *);

struct pciide_attach_args {
    const struct pci_device *pa_device;
    const struct pci_device *pa_isa_device;
    enum pciide_mode_policy pa_policy;
    unsigned pa_command_port;
    unsigned pa_control_port;
    unsigned pa_irq;
    unsigned pa_wait_ticks;
    pciide_irq_establish_t pa_irq_establish;
    pciide_wait_t pa_wait;
    pciide_wakeup_t pa_wakeup;
    void *pa_platform_cookie;
};

struct pciide_softc {
    struct pci_device ps_device;
    struct pci_device ps_isa_device;
    struct pci_resource ps_command;
    struct pci_resource ps_control;
    struct pci_resource ps_bus_master;
    struct dma_mem ps_prd_dma;
    struct dma_mem ps_buffer_dma;
    struct dma_map ps_data_map;
    struct disk_backend_ops ps_disk_ops;
    unsigned short ps_identify[256];
    disk_sector_t ps_sector_count;
    enum pciide_mode_policy ps_policy;
    enum pciide_transfer_mode ps_mode;
    enum pciide_dma_protocol ps_dma_protocol;
    unsigned ps_dma_mode;
    unsigned ps_pio_mode;
    unsigned ps_irq;
    unsigned ps_wait_ticks;
    unsigned ps_attached;
    unsigned ps_present;
    unsigned ps_flush_supported;
    unsigned ps_dma_ready;
    unsigned ps_dma_failed;
    unsigned ps_direct_dma_reported;
    volatile unsigned ps_dma_active;
    volatile unsigned ps_dma_done;
    volatile unsigned ps_dma_error;
    volatile unsigned ps_dma_bm_status;
    volatile unsigned ps_dma_ata_status;
    pciide_wait_t ps_wait;
    pciide_wakeup_t ps_wakeup;
    void *ps_platform_cookie;
};

int pciide_attach(struct pciide_softc *, const struct pciide_attach_args *);
const struct disk_backend_ops *pciide_disk_ops(struct pciide_softc *);
disk_sector_t pciide_sector_count(const struct pciide_softc *);
enum pciide_transfer_mode pciide_transfer_mode(const struct pciide_softc *);
enum pciide_dma_protocol pciide_dma_protocol(const struct pciide_softc *);
unsigned pciide_dma_mode(const struct pciide_softc *);
int pciide_interrupt(void *);

#endif /* _PCI_PCIIDE_H_ */
