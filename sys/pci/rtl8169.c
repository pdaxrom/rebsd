/*
 * Machine-independent Realtek RTL8169/RTL8110 PCI Ethernet hardware driver.
 *
 * The descriptor format, reset sequence and register programming follow the
 * public RTL8169 driver interfaces used by BSD and Linux.  PCI configuration,
 * BAR access, interrupt routing and DMA coherency are supplied exclusively by
 * the ReBSD machine-independent PCI and DMA contracts.
 */

#include <sys/param.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <pci/rtl8169.h>

#define RTL_REG_MAC0                    0x00u
#define RTL_REG_MAR0                    0x08u
#define RTL_REG_TX_DESC_LOW             0x20u
#define RTL_REG_TX_DESC_HIGH            0x24u
#define RTL_REG_CHIP_COMMAND            0x37u
#define RTL_REG_TX_POLL                 0x38u
#define RTL_REG_INTERRUPT_MASK          0x3cu
#define RTL_REG_INTERRUPT_STATUS        0x3eu
#define RTL_REG_TX_CONFIG               0x40u
#define RTL_REG_RX_CONFIG               0x44u
#define RTL_REG_CFG9346                 0x50u
#define RTL_REG_CONFIG2                 0x53u
#define RTL_REG_PHY_ACCESS              0x60u
#define RTL_REG_PHY_STATUS              0x6cu
#define RTL_REG_MAGIC                   0x7cu
#define RTL_REG_CPLUS_COMMAND           0xe0u
#define RTL_REG_INTERRUPT_MITIGATE      0xe2u
#define RTL_REG_RX_DESC_LOW             0xe4u
#define RTL_REG_RX_DESC_HIGH            0xe8u
#define RTL_REG_RX_MAX_SIZE             0xdau
#define RTL_REG_EARLY_TX_THRESHOLD      0xecu

#define RTL_COMMAND_RESET               0x10u
#define RTL_COMMAND_RX_ENABLE           0x08u
#define RTL_COMMAND_TX_ENABLE           0x04u
#define RTL_TX_POLL_NORMAL              0x40u
#define RTL_CFG9346_UNLOCK              0xc0u
#define RTL_CFG9346_LOCK                0x00u

#define RTL_INTERRUPT_SYSTEM_ERROR      0x8000u
#define RTL_INTERRUPT_TX_UNAVAILABLE    0x0080u
#define RTL_INTERRUPT_RX_FIFO_OVER      0x0040u
#define RTL_INTERRUPT_LINK_CHANGE       0x0020u
#define RTL_INTERRUPT_RX_OVERFLOW       0x0010u
#define RTL_INTERRUPT_TX_ERROR          0x0008u
#define RTL_INTERRUPT_TX_OK             0x0004u
#define RTL_INTERRUPT_RX_ERROR          0x0002u
#define RTL_INTERRUPT_RX_OK             0x0001u

#define RTL_DESCRIPTOR_OWN              0x80000000u
#define RTL_DESCRIPTOR_END              0x40000000u
#define RTL_DESCRIPTOR_FIRST            0x20000000u
#define RTL_DESCRIPTOR_LAST             0x10000000u
#define RTL_DESCRIPTOR_RX_ERROR         0x00200000u
#define RTL_DESCRIPTOR_LENGTH           0x00003fffu

#define RTL_RX_FIFO_THRESHOLD           (7u << 13)
#define RTL_RX_DMA_BURST                (7u << 8)
#define RTL_RX_ACCEPT_BROADCAST         0x08u
#define RTL_RX_ACCEPT_MULTICAST         0x04u
#define RTL_RX_ACCEPT_PHYSICAL          0x02u
#define RTL_TX_DMA_BURST                (7u << 8)
#define RTL_TX_INTERFRAME_GAP            (3u << 24)

#define RTL_CPLUS_NORMAL_MODE           0x2000u
#define RTL_CPLUS_RX_VLAN               0x0040u
#define RTL_CPLUS_RX_CHECKSUM           0x0020u
#define RTL_CPLUS_INTERRUPT_MASK        0x0003u
#define RTL_CPLUS_PCI_MULTIREAD         0x0008u
#define RTL_CPLUS_ANALOG_PLL            0x4000u

#define RTL_PHY_STATUS_TBI              0x80u
#define RTL_PHY_STATUS_LINK             0x02u
#define RTL_PHY_ACCESS_FLAG             0x80000000u
#define RTL_PHY_BMCR                    0u
#define RTL_PHY_ADVERTISE               4u
#define RTL_PHY_CONTROL_1000            9u
#define RTL_BMCR_AUTONEG_ENABLE         0x1000u
#define RTL_BMCR_AUTONEG_RESTART        0x0200u
#define RTL_ADVERTISE_ALL               0x01e1u
#define RTL_ADVERTISE_1000              0x0300u

#define RTL_PCI_CACHE_LINE_SIZE         0x0cu
#define RTL_PCI_LATENCY_TIMER           0x0du
#define RTL_REGISTER_BYTES              256u
#define RTL_RESET_ATTEMPTS              100u
#define RTL_PHY_ATTEMPTS                100u
#define RTL_DMA_ALIGNMENT               256u
#define RTL_DMA_BUFFER_ALIGNMENT        16u
#define RTL_RX_CRC_BYTES                4u
#define RTL_RX_MAX_SIZE                 (16u * 1024u)

#define RTL_CPLUS_PRESERVE              (RTL_CPLUS_NORMAL_MODE | \
                                        RTL_CPLUS_RX_VLAN | \
                                        RTL_CPLUS_RX_CHECKSUM | \
                                        RTL_CPLUS_INTERRUPT_MASK)

#if defined(TARGET_LITTLE_ENDIAN) || defined(__MIPSEL__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define RTL_NATIVE_LITTLE_ENDIAN 1
#else
#define RTL_NATIVE_LITTLE_ENDIAN 0
static unsigned
rtl_bswap32(unsigned value)
{
    return ((value & 0x000000ffu) << 24) |
        ((value & 0x0000ff00u) << 8) |
        ((value & 0x00ff0000u) >> 8) |
        ((value & 0xff000000u) >> 24);
}
#endif

static unsigned
rtl_to_le32(unsigned value)
{
#if RTL_NATIVE_LITTLE_ENDIAN
    return value;
#else
    return rtl_bswap32(value);
#endif
}

static unsigned
rtl_from_le32(unsigned value)
{
    return rtl_to_le32(value);
}

static unsigned
rtl_align(unsigned value, unsigned alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static unsigned char
rtl_read8(struct rtl8169_softc *sc, unsigned offset)
{
    return pci_resource_read8(&sc->rs_registers, offset);
}

static unsigned short
rtl_read16(struct rtl8169_softc *sc, unsigned offset)
{
    return pci_resource_read16(&sc->rs_registers, offset);
}

static unsigned
rtl_read32(struct rtl8169_softc *sc, unsigned offset)
{
    return pci_resource_read32(&sc->rs_registers, offset);
}

static void
rtl_write8(struct rtl8169_softc *sc, unsigned offset, unsigned char value)
{
    pci_resource_write8(&sc->rs_registers, offset, value);
}

static void
rtl_write16(struct rtl8169_softc *sc, unsigned offset, unsigned short value)
{
    pci_resource_write16(&sc->rs_registers, offset, value);
}

static void
rtl_write32(struct rtl8169_softc *sc, unsigned offset, unsigned value)
{
    pci_resource_write32(&sc->rs_registers, offset, value);
}

static int
rtl_reset(struct rtl8169_softc *sc)
{
    unsigned attempt;

    rtl_write16(sc, RTL_REG_INTERRUPT_MASK, 0);
    rtl_write16(sc, RTL_REG_INTERRUPT_STATUS, 0xffffu);
    rtl_write8(sc, RTL_REG_CHIP_COMMAND, RTL_COMMAND_RESET);
    for (attempt = 0; attempt < RTL_RESET_ATTEMPTS; ++attempt) {
        if ((rtl_read8(sc, RTL_REG_CHIP_COMMAND) &
            RTL_COMMAND_RESET) == 0)
            return 0;
        pci_delay_us(&sc->rs_device, 100);
    }
    return ETIMEDOUT;
}

static unsigned
rtl_mac_version(unsigned xid)
{
    switch (xid) {
    case 0x008u:
        return 2;
    case 0x040u:
        return 3;
    case 0x100u:
        return 4;
    case 0x180u:
        return 5;
    case 0x980u:
        return 6;
    default:
        return 0;
    }
}

static int
rtl_enaddr_valid(const unsigned char *address)
{
    unsigned i;
    unsigned all_zero;
    unsigned all_ones;

    all_zero = 1;
    all_ones = 1;
    for (i = 0; i < 6u; ++i) {
        if (address[i] != 0)
            all_zero = 0;
        if (address[i] != 0xffu)
            all_ones = 0;
    }
    return !all_zero && !all_ones && (address[0] & 1u) == 0;
}

static int
rtl_map_registers(struct rtl8169_softc *sc)
{
    int error;

    error = pci_map_bar(&sc->rs_device, 1, RTL_REGISTER_BYTES,
        &sc->rs_registers);
    if (error == 0 && sc->rs_registers.pr_type == PCI_RESOURCE_MEMORY)
        return 0;
    error = pci_map_bar(&sc->rs_device, 0, RTL_REGISTER_BYTES,
        &sc->rs_registers);
    if (error != 0)
        return error;
    return sc->rs_registers.pr_type == PCI_RESOURCE_IO ? 0 : EINVAL;
}

static int
rtl_dma_layout(struct rtl8169_softc *sc)
{
    unsigned offset;
    unsigned size;
    int error;

    offset = 0;
    sc->rs_rx_desc_offset = offset;
    offset += RTL8169_RX_DESCRIPTORS *
        sizeof(struct rtl8169_descriptor);
    offset = rtl_align(offset, RTL_DMA_ALIGNMENT);
    sc->rs_tx_desc_offset = offset;
    offset += RTL8169_TX_DESCRIPTORS *
        sizeof(struct rtl8169_descriptor);
    offset = rtl_align(offset, RTL_DMA_BUFFER_ALIGNMENT);
    sc->rs_rx_buffers_offset = offset;
    offset += RTL8169_RX_DESCRIPTORS * RTL8169_FRAME_BYTES;
    offset = rtl_align(offset, RTL_DMA_BUFFER_ALIGNMENT);
    sc->rs_tx_buffers_offset = offset;
    offset += RTL8169_TX_DESCRIPTORS * RTL8169_FRAME_BYTES;
    size = offset;

    error = dma_alloc(&sc->rs_dma, size, RTL_DMA_ALIGNMENT,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0)
        return error;
    sc->rs_rx_desc = (struct rtl8169_descriptor *)
        ((unsigned char *)sc->rs_dma.dm_vaddr + sc->rs_rx_desc_offset);
    sc->rs_tx_desc = (struct rtl8169_descriptor *)
        ((unsigned char *)sc->rs_dma.dm_vaddr + sc->rs_tx_desc_offset);
    sc->rs_rx_buffers = (unsigned char *)sc->rs_dma.dm_vaddr +
        sc->rs_rx_buffers_offset;
    sc->rs_tx_buffers = (unsigned char *)sc->rs_dma.dm_vaddr +
        sc->rs_tx_buffers_offset;
    return 0;
}

static dma_addr_t
rtl_dma_address(struct rtl8169_softc *sc, unsigned offset)
{
    return sc->rs_dma.dm_paddr + offset;
}

static void
rtl_descriptor_address(struct rtl8169_descriptor *descriptor,
    dma_addr_t address)
{
    descriptor->rd_address_low = rtl_to_le32(address);
    descriptor->rd_address_high = 0;
}

static void
rtl_initialize_rings(struct rtl8169_softc *sc)
{
    struct rtl8169_descriptor *descriptor;
    unsigned flags;
    unsigned i;

    bzero((caddr_t)sc->rs_rx_desc, RTL8169_RX_DESCRIPTORS *
        sizeof(*sc->rs_rx_desc));
    bzero((caddr_t)sc->rs_tx_desc, RTL8169_TX_DESCRIPTORS *
        sizeof(*sc->rs_tx_desc));
    for (i = 0; i < RTL8169_RX_DESCRIPTORS; ++i) {
        descriptor = &sc->rs_rx_desc[i];
        rtl_descriptor_address(descriptor, rtl_dma_address(sc,
            sc->rs_rx_buffers_offset + i * RTL8169_FRAME_BYTES));
        flags = RTL_DESCRIPTOR_OWN | RTL8169_FRAME_BYTES;
        if (i + 1u == RTL8169_RX_DESCRIPTORS)
            flags |= RTL_DESCRIPTOR_END;
        descriptor->rd_opts1 = rtl_to_le32(flags);
    }
    for (i = 0; i < RTL8169_TX_DESCRIPTORS; ++i) {
        descriptor = &sc->rs_tx_desc[i];
        rtl_descriptor_address(descriptor, rtl_dma_address(sc,
            sc->rs_tx_buffers_offset + i * RTL8169_FRAME_BYTES));
        if (i + 1u == RTL8169_TX_DESCRIPTORS)
            descriptor->rd_opts1 = rtl_to_le32(RTL_DESCRIPTOR_END);
    }
    sc->rs_rx_head = 0;
    sc->rs_tx_head = 0;
    sc->rs_tx_tail = 0;
    sc->rs_tx_used = 0;
    (void)dma_sync_for_device(&sc->rs_dma, 0, sc->rs_dma.dm_size,
        DMA_BIDIRECTIONAL);
}

static int
rtl_phy_write(struct rtl8169_softc *sc, unsigned reg, unsigned value)
{
    unsigned attempt;

    rtl_write32(sc, RTL_REG_PHY_ACCESS, RTL_PHY_ACCESS_FLAG |
        ((reg & 0x1fu) << 16) | (value & 0xffffu));
    for (attempt = 0; attempt < RTL_PHY_ATTEMPTS; ++attempt) {
        if ((rtl_read32(sc, RTL_REG_PHY_ACCESS) &
            RTL_PHY_ACCESS_FLAG) == 0)
            return 0;
        pci_delay_us(&sc->rs_device, 100);
    }
    return ETIMEDOUT;
}

static int
rtl_phy_read(struct rtl8169_softc *sc, unsigned reg, unsigned *value)
{
    unsigned attempt;
    unsigned result;

    rtl_write32(sc, RTL_REG_PHY_ACCESS, (reg & 0x1fu) << 16);
    for (attempt = 0; attempt < RTL_PHY_ATTEMPTS; ++attempt) {
        result = rtl_read32(sc, RTL_REG_PHY_ACCESS);
        if (result & RTL_PHY_ACCESS_FLAG) {
            *value = result & 0xffffu;
            return 0;
        }
        pci_delay_us(&sc->rs_device, 100);
    }
    return ETIMEDOUT;
}

static int
rtl_phy_autonegotiate(struct rtl8169_softc *sc)
{
    unsigned value;
    int error;

    if (rtl_read8(sc, RTL_REG_PHY_STATUS) & RTL_PHY_STATUS_TBI)
        return EOPNOTSUPP;
    error = rtl_phy_read(sc, RTL_PHY_ADVERTISE, &value);
    if (error != 0)
        return error;
    error = rtl_phy_write(sc, RTL_PHY_ADVERTISE,
        value | RTL_ADVERTISE_ALL);
    if (error != 0)
        return error;
    error = rtl_phy_read(sc, RTL_PHY_CONTROL_1000, &value);
    if (error != 0)
        return error;
    error = rtl_phy_write(sc, RTL_PHY_CONTROL_1000,
        value | RTL_ADVERTISE_1000);
    if (error != 0)
        return error;
    error = rtl_phy_read(sc, RTL_PHY_BMCR, &value);
    if (error != 0)
        return error;
    return rtl_phy_write(sc, RTL_PHY_BMCR,
        value | RTL_BMCR_AUTONEG_ENABLE | RTL_BMCR_AUTONEG_RESTART);
}

static void
rtl_program_magic(struct rtl8169_softc *sc)
{
    unsigned value;

    if (sc->rs_mac_version == 5)
        value = 0x000fff00u;
    else if (sc->rs_mac_version == 6)
        value = 0x00ffff00u;
    else
        return;
    if (rtl_read8(sc, RTL_REG_CONFIG2) & 1u)
        value |= 0xffu;
    rtl_write32(sc, RTL_REG_MAGIC, value);
}

int
rtl8169_attach(struct rtl8169_softc *sc, const struct pci_device *device,
    const struct rtl8169_callbacks *callbacks, void *callback_arg)
{
    unsigned command;
    unsigned char cache_line;
    unsigned i;
    unsigned char latency;
    int error;

    if (sc == 0 || device == 0 || callbacks == 0 ||
        callbacks->rc_receive == 0 || callbacks->rc_receive_error == 0 ||
        callbacks->rc_transmit_done == 0 ||
        callbacks->rc_link_change == 0)
        return EINVAL;
    if (device->pd_vendor != RTL8169_VENDOR_REALTEK ||
        device->pd_product != RTL8169_PRODUCT_8169 ||
        device->pd_class != PCI_CLASS_NETWORK ||
        device->pd_subclass != PCI_SUBCLASS_ETHERNET)
        return ENXIO;

    bzero((caddr_t)sc, sizeof(*sc));
    sc->rs_device = *device;
    sc->rs_callbacks = callbacks;
    sc->rs_callback_arg = callback_arg;
    command = pci_config_read16(device, PCI_CONFIG_COMMAND_STATUS);
    cache_line = pci_config_read8(device, RTL_PCI_CACHE_LINE_SIZE);
    latency = pci_config_read8(device, RTL_PCI_LATENCY_TIMER);
    error = rtl_map_registers(sc);
    if (error != 0)
        goto fail;
    error = pci_device_enable(device, PCI_COMMAND_MASTER |
        (sc->rs_registers.pr_type == PCI_RESOURCE_MEMORY ?
        PCI_COMMAND_MEMORY : PCI_COMMAND_IO));
    if (error != 0)
        goto fail;
    pci_config_write8(device, RTL_PCI_CACHE_LINE_SIZE, 0x08u);
    pci_config_write8(device, RTL_PCI_LATENCY_TIMER, 0x40u);
    error = rtl_reset(sc);
    if (error != 0)
        goto fail;

    sc->rs_xid = (rtl_read32(sc, RTL_REG_TX_CONFIG) >> 20) & 0xfcfu;
    sc->rs_mac_version = rtl_mac_version(sc->rs_xid);
    if (sc->rs_mac_version == 0) {
        error = EOPNOTSUPP;
        goto fail;
    }
    for (i = 0; i < sizeof(sc->rs_enaddr); ++i)
        sc->rs_enaddr[i] = rtl_read8(sc, RTL_REG_MAC0 + i);
    if (!rtl_enaddr_valid(sc->rs_enaddr)) {
        error = EADDRNOTAVAIL;
        goto fail;
    }
    error = rtl_dma_layout(sc);
    if (error != 0)
        goto fail;
    sc->rs_irq_mask = RTL_INTERRUPT_SYSTEM_ERROR |
        RTL_INTERRUPT_TX_UNAVAILABLE | RTL_INTERRUPT_RX_FIFO_OVER |
        RTL_INTERRUPT_LINK_CHANGE |
        RTL_INTERRUPT_RX_OVERFLOW | RTL_INTERRUPT_TX_ERROR |
        RTL_INTERRUPT_TX_OK | RTL_INTERRUPT_RX_ERROR |
        RTL_INTERRUPT_RX_OK;
    error = pci_interrupt_establish(device, rtl8169_interrupt, sc);
    if (error != 0)
        goto fail_dma;
    sc->rs_attached = 1;
    return 0;

fail_dma:
    (void)dma_free(&sc->rs_dma);
fail:
    pci_config_write8(device, RTL_PCI_CACHE_LINE_SIZE, cache_line);
    pci_config_write8(device, RTL_PCI_LATENCY_TIMER, latency);
    pci_config_write16(device, PCI_CONFIG_COMMAND_STATUS,
        (unsigned short)command);
    return error;
}

int
rtl8169_start(struct rtl8169_softc *sc)
{
    dma_addr_t rx_address;
    dma_addr_t tx_address;
    unsigned cplus;
    int error;

    if (sc == 0 || !sc->rs_attached)
        return ENXIO;
    error = rtl_reset(sc);
    if (error != 0)
        return error;
    rtl_initialize_rings(sc);
    rtl_write8(sc, RTL_REG_CFG9346, RTL_CFG9346_UNLOCK);
    rtl_write8(sc, RTL_REG_EARLY_TX_THRESHOLD, 0x3fu);
    cplus = rtl_read16(sc, RTL_REG_CPLUS_COMMAND) & RTL_CPLUS_PRESERVE;
    cplus |= RTL_CPLUS_PCI_MULTIREAD;
    if (sc->rs_mac_version == 2 || sc->rs_mac_version == 3)
        cplus |= RTL_CPLUS_ANALOG_PLL;
    rtl_write16(sc, RTL_REG_CPLUS_COMMAND, (unsigned short)cplus);
    rtl_program_magic(sc);
    rtl_write16(sc, RTL_REG_INTERRUPT_MITIGATE, 0);
    rtl_write16(sc, RTL_REG_RX_MAX_SIZE, RTL_RX_MAX_SIZE);

    tx_address = rtl_dma_address(sc, sc->rs_tx_desc_offset);
    rx_address = rtl_dma_address(sc, sc->rs_rx_desc_offset);
    rtl_write32(sc, RTL_REG_TX_DESC_HIGH, 0);
    rtl_write32(sc, RTL_REG_TX_DESC_LOW, tx_address);
    rtl_write32(sc, RTL_REG_RX_DESC_HIGH, 0);
    rtl_write32(sc, RTL_REG_RX_DESC_LOW, rx_address);
    rtl_write8(sc, RTL_REG_CFG9346, RTL_CFG9346_LOCK);

    rtl_write32(sc, RTL_REG_MAR0, 0xffffffffu);
    rtl_write32(sc, RTL_REG_MAR0 + 4u, 0xffffffffu);
    rtl_write8(sc, RTL_REG_CHIP_COMMAND,
        RTL_COMMAND_TX_ENABLE | RTL_COMMAND_RX_ENABLE);
    rtl_write32(sc, RTL_REG_RX_CONFIG, RTL_RX_FIFO_THRESHOLD |
        RTL_RX_DMA_BURST | RTL_RX_ACCEPT_BROADCAST |
        RTL_RX_ACCEPT_MULTICAST | RTL_RX_ACCEPT_PHYSICAL);
    rtl_write32(sc, RTL_REG_TX_CONFIG,
        RTL_TX_DMA_BURST | RTL_TX_INTERFRAME_GAP);
    rtl_write16(sc, RTL_REG_INTERRUPT_STATUS, 0xffffu);
    rtl_write16(sc, RTL_REG_INTERRUPT_MASK,
        (unsigned short)sc->rs_irq_mask);
    sc->rs_running = 1;
    error = rtl_phy_autonegotiate(sc);
    if (error != 0) {
        rtl8169_stop(sc);
        return error;
    }
    sc->rs_callbacks->rc_link_change(sc->rs_callback_arg,
        rtl8169_link_up(sc));
    return 0;
}

void
rtl8169_stop(struct rtl8169_softc *sc)
{
    if (sc == 0 || !sc->rs_attached)
        return;
    rtl_write16(sc, RTL_REG_INTERRUPT_MASK, 0);
    rtl_write16(sc, RTL_REG_INTERRUPT_STATUS, 0xffffu);
    rtl_write8(sc, RTL_REG_CHIP_COMMAND, 0);
    sc->rs_running = 0;
}

unsigned
rtl8169_tx_available(const struct rtl8169_softc *sc)
{
    if (sc == 0 || !sc->rs_running)
        return 0;
    return RTL8169_TX_DESCRIPTORS - sc->rs_tx_used;
}

int
rtl8169_transmit(struct rtl8169_softc *sc, const unsigned char *frame,
    unsigned length)
{
    struct rtl8169_descriptor *descriptor;
    unsigned char *buffer;
    unsigned flags;
    unsigned index;

    if (sc == 0 || frame == 0 || !sc->rs_running)
        return ENETDOWN;
    if (length == 0 || length > RTL8169_FRAME_BYTES)
        return EMSGSIZE;
    if (sc->rs_tx_used >= RTL8169_TX_DESCRIPTORS)
        return EBUSY;
    index = sc->rs_tx_head;
    descriptor = &sc->rs_tx_desc[index];
    buffer = sc->rs_tx_buffers + index * RTL8169_FRAME_BYTES;
    bcopy((caddr_t)frame, (caddr_t)buffer, length);
    (void)dma_sync_for_device(&sc->rs_dma,
        sc->rs_tx_buffers_offset + index * RTL8169_FRAME_BYTES, length,
        DMA_TO_DEVICE);
    descriptor->rd_opts2 = 0;
    flags = RTL_DESCRIPTOR_FIRST | RTL_DESCRIPTOR_LAST | length;
    if (index + 1u == RTL8169_TX_DESCRIPTORS)
        flags |= RTL_DESCRIPTOR_END;
    descriptor->rd_opts1 = rtl_to_le32(flags | RTL_DESCRIPTOR_OWN);
    (void)dma_sync_for_device(&sc->rs_dma,
        sc->rs_tx_desc_offset + index * sizeof(*descriptor),
        sizeof(*descriptor), DMA_TO_DEVICE);
    sc->rs_tx_head = (index + 1u) % RTL8169_TX_DESCRIPTORS;
    sc->rs_tx_used++;
    rtl_write8(sc, RTL_REG_TX_POLL, RTL_TX_POLL_NORMAL);
    return 0;
}

static unsigned
rtl_reap_transmit(struct rtl8169_softc *sc, unsigned errors)
{
    struct rtl8169_descriptor *descriptor;
    unsigned completed;
    unsigned status;
    unsigned index;

    completed = 0;
    while (sc->rs_tx_used != 0) {
        index = sc->rs_tx_tail;
        descriptor = &sc->rs_tx_desc[index];
        (void)dma_sync_for_cpu(&sc->rs_dma,
            sc->rs_tx_desc_offset + index * sizeof(*descriptor),
            sizeof(*descriptor), DMA_FROM_DEVICE);
        status = rtl_from_le32(descriptor->rd_opts1);
        if (status & RTL_DESCRIPTOR_OWN)
            break;
        descriptor->rd_opts1 = rtl_to_le32(
            index + 1u == RTL8169_TX_DESCRIPTORS ?
            RTL_DESCRIPTOR_END : 0);
        sc->rs_tx_tail = (index + 1u) % RTL8169_TX_DESCRIPTORS;
        sc->rs_tx_used--;
        completed++;
    }
    if (completed != 0)
        sc->rs_callbacks->rc_transmit_done(sc->rs_callback_arg,
            completed, errors ? completed : 0);
    return completed;
}

static unsigned
rtl_receive(struct rtl8169_softc *sc)
{
    struct rtl8169_descriptor *descriptor;
    unsigned flags;
    unsigned length;
    unsigned processed;
    unsigned status;
    unsigned index;

    processed = 0;
    while (processed < RTL8169_RX_DESCRIPTORS) {
        index = sc->rs_rx_head;
        descriptor = &sc->rs_rx_desc[index];
        (void)dma_sync_for_cpu(&sc->rs_dma,
            sc->rs_rx_desc_offset + index * sizeof(*descriptor),
            sizeof(*descriptor), DMA_FROM_DEVICE);
        status = rtl_from_le32(descriptor->rd_opts1);
        if (status & RTL_DESCRIPTOR_OWN)
            break;
        length = status & RTL_DESCRIPTOR_LENGTH;
        if ((status & (RTL_DESCRIPTOR_FIRST | RTL_DESCRIPTOR_LAST)) ==
            (RTL_DESCRIPTOR_FIRST | RTL_DESCRIPTOR_LAST) &&
            (status & RTL_DESCRIPTOR_RX_ERROR) == 0 &&
            length >= RTL_RX_CRC_BYTES &&
            length <= RTL8169_FRAME_BYTES) {
            length -= RTL_RX_CRC_BYTES;
            (void)dma_sync_for_cpu(&sc->rs_dma,
                sc->rs_rx_buffers_offset + index * RTL8169_FRAME_BYTES,
                length, DMA_FROM_DEVICE);
            sc->rs_callbacks->rc_receive(sc->rs_callback_arg,
                sc->rs_rx_buffers + index * RTL8169_FRAME_BYTES, length);
        } else
            sc->rs_callbacks->rc_receive_error(sc->rs_callback_arg);
        (void)dma_sync_for_device(&sc->rs_dma,
            sc->rs_rx_buffers_offset + index * RTL8169_FRAME_BYTES,
            RTL8169_FRAME_BYTES, DMA_FROM_DEVICE);
        descriptor->rd_opts2 = 0;
        flags = RTL_DESCRIPTOR_OWN | RTL8169_FRAME_BYTES;
        if (index + 1u == RTL8169_RX_DESCRIPTORS)
            flags |= RTL_DESCRIPTOR_END;
        descriptor->rd_opts1 = rtl_to_le32(flags);
        (void)dma_sync_for_device(&sc->rs_dma,
            sc->rs_rx_desc_offset + index * sizeof(*descriptor),
            sizeof(*descriptor), DMA_FROM_DEVICE);
        sc->rs_rx_head = (index + 1u) % RTL8169_RX_DESCRIPTORS;
        processed++;
    }
    return processed;
}

int
rtl8169_interrupt(void *arg)
{
    struct rtl8169_softc *sc;
    unsigned status;

    sc = (struct rtl8169_softc *)arg;
    if (sc == 0 || !sc->rs_attached)
        return 0;
    status = rtl_read16(sc, RTL_REG_INTERRUPT_STATUS);
    if (status == 0 || status == 0xffffu ||
        (status & sc->rs_irq_mask) == 0)
        return 0;
    rtl_write16(sc, RTL_REG_INTERRUPT_STATUS, (unsigned short)status);
    if (!sc->rs_running)
        return 1;
    if (status & (RTL_INTERRUPT_RX_OK | RTL_INTERRUPT_RX_ERROR |
        RTL_INTERRUPT_RX_OVERFLOW | RTL_INTERRUPT_RX_FIFO_OVER))
        (void)rtl_receive(sc);
    if (status & (RTL_INTERRUPT_TX_OK | RTL_INTERRUPT_TX_ERROR |
        RTL_INTERRUPT_TX_UNAVAILABLE))
        (void)rtl_reap_transmit(sc,
            status & RTL_INTERRUPT_TX_ERROR);
    if (status & RTL_INTERRUPT_LINK_CHANGE)
        sc->rs_callbacks->rc_link_change(sc->rs_callback_arg,
            rtl8169_link_up(sc));
    if (status & RTL_INTERRUPT_SYSTEM_ERROR) {
        unsigned outstanding;

        outstanding = sc->rs_tx_used;
        rtl8169_stop(sc);
        if (outstanding != 0)
            sc->rs_callbacks->rc_transmit_done(sc->rs_callback_arg,
                outstanding, outstanding);
        (void)rtl8169_start(sc);
    }
    return 1;
}

int
rtl8169_link_up(const struct rtl8169_softc *sc)
{
    if (sc == 0 || !sc->rs_attached)
        return 0;
    return (pci_resource_read8(&sc->rs_registers,
        RTL_REG_PHY_STATUS) & RTL_PHY_STATUS_LINK) != 0;
}
