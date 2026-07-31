/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _PCI_RTL8169_H_
#define _PCI_RTL8169_H_

#include <sys/dma.h>
#include <pci/pci.h>

#define RTL8169_VENDOR_REALTEK          0x10ecu
#define RTL8169_PRODUCT_8169            0x8169u
#define RTL8169_TX_DESCRIPTORS          4u
#define RTL8169_RX_DESCRIPTORS          16u
#define RTL8169_FRAME_BYTES             2048u

struct rtl8169_descriptor {
    volatile unsigned rd_opts1;
    volatile unsigned rd_opts2;
    volatile unsigned rd_address_low;
    volatile unsigned rd_address_high;
};

struct rtl8169_callbacks {
    void (*rc_receive)(void *, const unsigned char *, unsigned);
    void (*rc_receive_error)(void *);
    void (*rc_transmit_done)(void *, unsigned, unsigned);
    void (*rc_link_change)(void *, int);
};

struct rtl8169_softc {
    struct pci_device rs_device;
    struct pci_resource rs_registers;
    struct dma_mem rs_dma;
    const struct rtl8169_callbacks *rs_callbacks;
    void *rs_callback_arg;
    struct rtl8169_descriptor *rs_rx_desc;
    struct rtl8169_descriptor *rs_tx_desc;
    unsigned char *rs_rx_buffers;
    unsigned char *rs_tx_buffers;
    unsigned rs_rx_desc_offset;
    unsigned rs_tx_desc_offset;
    unsigned rs_rx_buffers_offset;
    unsigned rs_tx_buffers_offset;
    unsigned rs_rx_head;
    unsigned rs_tx_head;
    unsigned rs_tx_tail;
    unsigned rs_tx_used;
    unsigned rs_mac_version;
    unsigned rs_xid;
    unsigned rs_irq_mask;
    unsigned rs_attached;
    unsigned rs_running;
    unsigned char rs_enaddr[6];
};

int rtl8169_attach(struct rtl8169_softc *, const struct pci_device *,
    const struct rtl8169_callbacks *, void *);
int rtl8169_start(struct rtl8169_softc *);
void rtl8169_stop(struct rtl8169_softc *);
int rtl8169_transmit(struct rtl8169_softc *, const unsigned char *, unsigned);
unsigned rtl8169_tx_available(const struct rtl8169_softc *);
int rtl8169_interrupt(void *);
int rtl8169_link_up(const struct rtl8169_softc *);

#endif /* _PCI_RTL8169_H_ */
