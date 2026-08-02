/*
 * Machine-independent PCI IDE primary-channel driver.
 *
 * PCI configuration, resource access, DMA memory and interrupt routing are
 * supplied by the common ReBSD PCI/DMA contracts.  A platform adapter only
 * provides the legacy IRQ establishment and scheduler wait/wakeup boundary.
 */

#include <sys/param.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <pci/pciide.h>

#define ATA_REG_DATA                    0u
#define ATA_REG_ERROR                   1u
#define ATA_REG_FEATURES                1u
#define ATA_REG_SECTOR_COUNT            2u
#define ATA_REG_LBA_LOW                 3u
#define ATA_REG_LBA_MID                 4u
#define ATA_REG_LBA_HIGH                5u
#define ATA_REG_DRIVE                   6u
#define ATA_REG_STATUS                  7u
#define ATA_REG_COMMAND                 7u

#define ATA_STATUS_ERROR                0x01u
#define ATA_STATUS_DATA_REQUEST         0x08u
#define ATA_STATUS_DEVICE_FAULT         0x20u
#define ATA_STATUS_READY                0x40u
#define ATA_STATUS_BUSY                 0x80u

#define ATA_CONTROL_INTERRUPT_DISABLE   0x02u
#define ATA_CONTROL_SOFTWARE_RESET      0x04u

#define ATA_COMMAND_READ_SECTORS        0x20u
#define ATA_COMMAND_WRITE_SECTORS       0x30u
#define ATA_COMMAND_READ_DMA            0xc8u
#define ATA_COMMAND_WRITE_DMA           0xcau
#define ATA_COMMAND_FLUSH_CACHE         0xe7u
#define ATA_COMMAND_IDENTIFY            0xecu
#define ATA_COMMAND_SET_FEATURES        0xefu

#define ATA_FEATURE_SET_TRANSFER_MODE   0x03u
#define ATA_TRANSFER_PIO_FLOW           0x08u
#define ATA_TRANSFER_MWDMA              0x20u

#define ATA_IDENTIFY_MODEL_FIRST        27u
#define ATA_IDENTIFY_MODEL_WORDS        20u
#define ATA_IDENTIFY_CAPABILITIES       49u
#define ATA_IDENTIFY_VALIDITY           53u
#define ATA_IDENTIFY_MWDMA              63u
#define ATA_IDENTIFY_PIO                64u
#define ATA_IDENTIFY_LBA28_LOW          60u
#define ATA_IDENTIFY_LBA28_HIGH         61u
#define ATA_IDENTIFY_COMMAND_SET_2      83u
#define ATA_CAPABILITY_DMA              0x0100u
#define ATA_CAPABILITY_LBA              0x0200u
#define ATA_VALIDITY_WORDS_64_70        0x0002u
#define ATA_FLUSH_CACHE_SUPPORTED       0x1000u

#define PCIIDE_INTERFACE_PRIMARY_NATIVE 0x01u
#define PCIIDE_INTERFACE_BUS_MASTER     0x80u
#define PCIIDE_BUS_MASTER_BAR           4u
#define PCIIDE_BUS_MASTER_BYTES         16u
#define PCIIDE_BM_COMMAND               0u
#define PCIIDE_BM_STATUS                2u
#define PCIIDE_BM_PRDT                  4u
#define PCIIDE_BM_COMMAND_START         0x01u
#define PCIIDE_BM_COMMAND_READ          0x08u
#define PCIIDE_BM_STATUS_ACTIVE         0x01u
#define PCIIDE_BM_STATUS_ERROR          0x02u
#define PCIIDE_BM_STATUS_INTERRUPT      0x04u
#define PCIIDE_BM_STATUS_DRIVE0_DMA     0x20u

#define PCIIDE_PRD_END                  0x80000000u
#define PCIIDE_PRD_BYTES                8u
#define PCIIDE_PRD_ALIGNMENT            4u
#define PCIIDE_DMA_BUFFER_ALIGNMENT     0x10000u
#define PCIIDE_DMA_MAX_SECTORS          \
    (PCIIDE_DMA_BUFFER_BYTES / DISK_SECTOR_SIZE)

#define PCIIDE_VENDOR_INTEL             0x8086u
#define PCIIDE_PRODUCT_PIIX3            0x7010u
#define PCIIDE_VENDOR_VIA               0x1106u
#define PCIIDE_PRODUCT_VIA_IDE          0x0571u
#define PCIIDE_PRODUCT_VIA_596          0x0596u

#define PIIX_IDETIM                     0x40u
#define PIIX_IDETIM_IDE                 0x8000u
#define PIIX_IDETIM_ISP_SHIFT           12u
#define PIIX_IDETIM_RTC_SHIFT           8u
#define PIIX_IDETIM_DTE_DRIVE0          0x0008u
#define PIIX_IDETIM_TIME_DRIVE0         0x0001u

#define VIA_IDECONF                     0x40u
#define VIA_CTLMISC                     0x44u
#define VIA_DATATIM                     0x48u
#define VIA_MISCTIM                     0x4cu
#define VIA_UDMA                        0x50u
#define VIA_IDECONF_PRIMARY_ENABLE      0x00000002u

#define PCIIDE_POLL_ATTEMPTS            100000u
#define PCIIDE_RESET_SETTLE_US          2000u

static const unsigned char piix_mwdma_isp[] = { 0u, 2u, 2u };
static const unsigned char piix_mwdma_rtc[] = { 0u, 2u, 3u };
static const unsigned char via_pio_pulse[] = { 10u, 10u, 10u, 2u, 2u };
static const unsigned char via_pio_recovery[] = { 8u, 8u, 8u, 2u, 0u };

static void
pciide_zero(void *data_arg, size_t size)
{
    unsigned char *data;

    data = (unsigned char *)data_arg;
    while (size-- != 0)
        *data++ = 0;
}

static void
pciide_copy(void *destination_arg, const void *source_arg, size_t size)
{
    unsigned char *destination;
    const unsigned char *source;

    destination = (unsigned char *)destination_arg;
    source = (const unsigned char *)source_arg;
    while (size-- != 0)
        *destination++ = *source++;
}

static void
pciide_store_le32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static unsigned char
pciide_command_read8(struct pciide_softc *sc, size_t offset)
{
    return pci_resource_read8(&sc->ps_command, offset);
}

static unsigned short
pciide_command_read16(struct pciide_softc *sc)
{
    return pci_resource_read16(&sc->ps_command, ATA_REG_DATA);
}

static void
pciide_command_write8(struct pciide_softc *sc, size_t offset,
    unsigned char value)
{
    pci_resource_write8(&sc->ps_command, offset, value);
}

static void
pciide_command_write16(struct pciide_softc *sc, unsigned short value)
{
    pci_resource_write16(&sc->ps_command, ATA_REG_DATA, value);
}

static void
pciide_control_write(struct pciide_softc *sc, unsigned char value)
{
    pci_resource_write8(&sc->ps_control, 0, value);
}

static int
pciide_wait_status(struct pciide_softc *sc, unsigned char required,
    unsigned char forbidden, int reject_errors)
{
    unsigned char status;
    unsigned attempt;

    status = 0xffu;
    for (attempt = 0; attempt < PCIIDE_POLL_ATTEMPTS; ++attempt) {
        status = pciide_command_read8(sc, ATA_REG_STATUS);
        if (status == 0 || status == 0xffu)
            return ENXIO;
        if ((status & ATA_STATUS_BUSY) != 0) {
            pci_delay_us(&sc->ps_device, 1);
            continue;
        }
        if (reject_errors &&
            (status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0)
            return EIO;
        if ((status & required) == required &&
            (status & forbidden) == 0)
            return 0;
        pci_delay_us(&sc->ps_device, 1);
    }
    return ETIMEDOUT;
}

static int
pciide_wait_not_busy(struct pciide_softc *sc, int reject_errors)
{
    return pciide_wait_status(sc, 0, ATA_STATUS_BUSY, reject_errors);
}

static int
pciide_wait_data(struct pciide_softc *sc)
{
    return pciide_wait_status(sc, ATA_STATUS_DATA_REQUEST,
        ATA_STATUS_BUSY, 1);
}

static void
pciide_select_lba(struct pciide_softc *sc, unsigned lba)
{
    pciide_command_write8(sc, ATA_REG_DRIVE,
        (unsigned char)(0xe0u | ((lba >> 24) & 0x0fu)));
    pci_delay_us(&sc->ps_device, 1);
}

static void
pciide_program_lba(struct pciide_softc *sc, unsigned lba, unsigned count)
{
    pciide_select_lba(sc, lba);
    pciide_command_write8(sc, ATA_REG_SECTOR_COUNT, (unsigned char)count);
    pciide_command_write8(sc, ATA_REG_LBA_LOW, (unsigned char)lba);
    pciide_command_write8(sc, ATA_REG_LBA_MID, (unsigned char)(lba >> 8));
    pciide_command_write8(sc, ATA_REG_LBA_HIGH,
        (unsigned char)(lba >> 16));
}

static int
pciide_identify(struct pciide_softc *sc)
{
    unsigned char high;
    unsigned char mid;
    unsigned i;
    int error;

    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    pciide_command_write8(sc, ATA_REG_DRIVE, 0xa0u);
    pci_delay_us(&sc->ps_device, 1);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    pciide_command_write8(sc, ATA_REG_SECTOR_COUNT, 0);
    pciide_command_write8(sc, ATA_REG_LBA_LOW, 0);
    pciide_command_write8(sc, ATA_REG_LBA_MID, 0);
    pciide_command_write8(sc, ATA_REG_LBA_HIGH, 0);
    pciide_command_write8(sc, ATA_REG_COMMAND, ATA_COMMAND_IDENTIFY);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    mid = pciide_command_read8(sc, ATA_REG_LBA_MID);
    high = pciide_command_read8(sc, ATA_REG_LBA_HIGH);
    if (mid != 0 || high != 0)
        return EOPNOTSUPP;
    error = pciide_wait_data(sc);
    if (error != 0)
        return error;
    for (i = 0; i < 256u; ++i)
        sc->ps_identify[i] = pciide_command_read16(sc);
    return pciide_wait_not_busy(sc, 1);
}

static void
pciide_print_model(struct pciide_softc *sc)
{
    char model[ATA_IDENTIFY_MODEL_WORDS * 2u + 1u];
    unsigned short word;
    unsigned end;
    unsigned i;

    for (i = 0; i < ATA_IDENTIFY_MODEL_WORDS; ++i) {
        word = sc->ps_identify[ATA_IDENTIFY_MODEL_FIRST + i];
        model[i * 2u] = (char)(word >> 8);
        model[i * 2u + 1u] = (char)word;
    }
    end = ATA_IDENTIFY_MODEL_WORDS * 2u;
    while (end != 0 && model[end - 1u] == ' ')
        --end;
    model[end] = '\0';
    printf("ata0: %s\n", model);
}

static int
pciide_pio_read_one(struct pciide_softc *sc, unsigned lba,
    unsigned char *data)
{
    unsigned short word;
    unsigned i;
    int error;

    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    pciide_program_lba(sc, lba, 1);
    pciide_command_write8(sc, ATA_REG_COMMAND, ATA_COMMAND_READ_SECTORS);
    error = pciide_wait_data(sc);
    if (error != 0)
        return error;
    for (i = 0; i < DISK_SECTOR_SIZE / 2u; ++i) {
        word = pciide_command_read16(sc);
        data[i * 2u] = (unsigned char)word;
        data[i * 2u + 1u] = (unsigned char)(word >> 8);
    }
    return pciide_wait_not_busy(sc, 1);
}

static int
pciide_pio_write_one(struct pciide_softc *sc, unsigned lba,
    const unsigned char *data)
{
    unsigned short word;
    unsigned i;
    int error;

    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    pciide_program_lba(sc, lba, 1);
    pciide_command_write8(sc, ATA_REG_COMMAND, ATA_COMMAND_WRITE_SECTORS);
    error = pciide_wait_data(sc);
    if (error != 0)
        return error;
    for (i = 0; i < DISK_SECTOR_SIZE / 2u; ++i) {
        word = (unsigned short)((unsigned)data[i * 2u] |
            ((unsigned)data[i * 2u + 1u] << 8));
        pciide_command_write16(sc, word);
    }
    return pciide_wait_not_busy(sc, 1);
}

static int
pciide_set_transfer_mode(struct pciide_softc *sc, unsigned char mode)
{
    int error;

    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    pciide_select_lba(sc, 0);
    pciide_command_write8(sc, ATA_REG_FEATURES,
        ATA_FEATURE_SET_TRANSFER_MODE);
    pciide_command_write8(sc, ATA_REG_SECTOR_COUNT, mode);
    pciide_command_write8(sc, ATA_REG_COMMAND, ATA_COMMAND_SET_FEATURES);
    return pciide_wait_not_busy(sc, 1);
}

static void
pciide_configure_piix(struct pciide_softc *sc, int dma,
    unsigned dma_mode)
{
    unsigned config;
    unsigned timing;

    config = pci_config_read32(&sc->ps_device, PIIX_IDETIM);
    config &= 0xffff0000u;
    timing = PIIX_IDETIM_IDE;
    if (dma) {
        timing |= (unsigned)piix_mwdma_isp[dma_mode] <<
            PIIX_IDETIM_ISP_SHIFT;
        timing |= (unsigned)piix_mwdma_rtc[dma_mode] <<
            PIIX_IDETIM_RTC_SHIFT;
        timing |= PIIX_IDETIM_DTE_DRIVE0 | PIIX_IDETIM_TIME_DRIVE0;
    }
    pci_config_write32(&sc->ps_device, PIIX_IDETIM, config | timing);
}

static void
pciide_configure_via(struct pciide_softc *sc, unsigned pio_mode)
{
    unsigned config;

    if (pio_mode > 4u)
        pio_mode = 0;
    config = pci_config_read32(&sc->ps_device, VIA_IDECONF);
    pci_config_write32(&sc->ps_device, VIA_IDECONF,
        config | VIA_IDECONF_PRIMARY_ENABLE);
    config = pci_config_read32(&sc->ps_device, VIA_DATATIM);
    config &= 0x00ffffffu;
    config |= (unsigned)via_pio_recovery[pio_mode] << 24;
    config |= (unsigned)via_pio_pulse[pio_mode] << 28;
    pci_config_write32(&sc->ps_device, VIA_DATATIM, config);
}

static int
pciide_controller_supported(struct pciide_softc *sc)
{
    if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_INTEL &&
        sc->ps_device.pd_product == PCIIDE_PRODUCT_PIIX3)
        return 1;
    if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_VIA &&
        sc->ps_device.pd_product == PCIIDE_PRODUCT_VIA_IDE &&
        sc->ps_isa_device.pd_vendor == PCIIDE_VENDOR_VIA &&
        sc->ps_isa_device.pd_product == PCIIDE_PRODUCT_VIA_596)
        return 1;
    return 0;
}

static void
pciide_configure_pio(struct pciide_softc *sc)
{
    if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_INTEL &&
        sc->ps_device.pd_product == PCIIDE_PRODUCT_PIIX3)
        pciide_configure_piix(sc, 0, 0);
    else if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_VIA &&
        sc->ps_device.pd_product == PCIIDE_PRODUCT_VIA_IDE)
        pciide_configure_via(sc, 0);
    (void)pciide_set_transfer_mode(sc, ATA_TRANSFER_PIO_FLOW);
}

static int
pciide_choose_mwdma(struct pciide_softc *sc, unsigned *mode)
{
    unsigned supported;

    if ((sc->ps_identify[ATA_IDENTIFY_CAPABILITIES] &
        ATA_CAPABILITY_DMA) == 0)
        return EOPNOTSUPP;
    supported = sc->ps_identify[ATA_IDENTIFY_MWDMA] & 0x07u;
    if (supported & 0x04u)
        *mode = 2;
    else if (supported & 0x02u)
        *mode = 1;
    else if (supported & 0x01u)
        *mode = 0;
    else
        return EOPNOTSUPP;

    return 0;
}

static void
pciide_via_pair_mwdma(struct pciide_softc *sc, unsigned *mode,
    unsigned *pio_mode)
{
    unsigned pio;

    /*
     * VIA uses the data-timing register for both PIO and multiword DMA.
     * Follow the BSD controller rule: MWDMA mode N needs the timing paired
     * with PIO mode N+2.  Words 64--70 are optional, so a device which does
     * not publish the advanced PIO modes is conservatively paired with
     * MWDMA0/PIO0.
     */
    pio = 2;
    if ((sc->ps_identify[ATA_IDENTIFY_VALIDITY] &
        ATA_VALIDITY_WORDS_64_70) != 0) {
        if ((sc->ps_identify[ATA_IDENTIFY_PIO] & 0x02u) != 0)
            pio = 4;
        else if ((sc->ps_identify[ATA_IDENTIFY_PIO] & 0x01u) != 0)
            pio = 3;
    }
    if (pio <= 2u) {
        *mode = 0;
        *pio_mode = 0;
    } else {
        if (*mode > pio - 2u)
            *mode = pio - 2u;
        *pio_mode = *mode + 2u;
    }
}

static void
pciide_dma_stop(struct pciide_softc *sc)
{
    unsigned char command;

    command = pci_resource_read8(&sc->ps_bus_master,
        PCIIDE_BM_COMMAND);
    pci_resource_write8(&sc->ps_bus_master, PCIIDE_BM_COMMAND,
        command & (unsigned char)~PCIIDE_BM_COMMAND_START);
}

static void
pciide_dma_clear_status(struct pciide_softc *sc)
{
    unsigned char status;

    status = pci_resource_read8(&sc->ps_bus_master, PCIIDE_BM_STATUS);
    pci_resource_write8(&sc->ps_bus_master, PCIIDE_BM_STATUS,
        (unsigned char)((status & 0x60u) |
        PCIIDE_BM_STATUS_DRIVE0_DMA | PCIIDE_BM_STATUS_ERROR |
        PCIIDE_BM_STATUS_INTERRUPT));
}

static int
pciide_soft_reset(struct pciide_softc *sc)
{
    pciide_dma_stop(sc);
    pciide_control_write(sc, ATA_CONTROL_SOFTWARE_RESET |
        ATA_CONTROL_INTERRUPT_DISABLE);
    pci_delay_us(&sc->ps_device, 5);
    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    pci_delay_us(&sc->ps_device, PCIIDE_RESET_SETTLE_US);
    return pciide_wait_not_busy(sc, 0);
}

static void
pciide_disable_dma(struct pciide_softc *sc)
{
    pciide_dma_stop(sc);
    pciide_dma_clear_status(sc);
    sc->ps_dma_active = 0;
    sc->ps_dma_done = 0;
    sc->ps_dma_ready = 0;
    sc->ps_dma_failed = 1;
    sc->ps_mode = PCIIDE_TRANSFER_PIO;
    (void)pciide_soft_reset(sc);
    pciide_configure_pio(sc);
    printf("ata0: DMA disabled after transfer failure; subsequent I/O uses PIO\n");
}

int
pciide_interrupt(void *arg)
{
    struct pciide_softc *sc;
    unsigned char status;
    unsigned char ata_status;

    sc = (struct pciide_softc *)arg;
    if (sc == 0 || !sc->ps_dma_active)
        return 0;
    status = pci_resource_read8(&sc->ps_bus_master, PCIIDE_BM_STATUS);
    ata_status = pciide_command_read8(sc, ATA_REG_STATUS);
    sc->ps_dma_bm_status = status;
    sc->ps_dma_ata_status = ata_status;

    /*
     * In compatibility mode the ATA device owns the dedicated channel IRQ.
     * Some controllers can deliver that IRQ before the bus-master interrupt
     * status bit is visible.  ATA guarantees that BSY is clear when INTRQ is
     * asserted, so do not discard a completed command solely because the
     * bus-master latch is late.
     */
    if ((status & (PCIIDE_BM_STATUS_ERROR |
        PCIIDE_BM_STATUS_INTERRUPT)) == 0 &&
        (ata_status & ATA_STATUS_BUSY) != 0)
        return 0;
    pciide_dma_stop(sc);
    sc->ps_dma_error =
        (status & PCIIDE_BM_STATUS_ERROR) != 0 ||
        ata_status == 0 || ata_status == 0xffu ||
        (ata_status & (ATA_STATUS_BUSY | ATA_STATUS_ERROR |
        ATA_STATUS_DEVICE_FAULT)) != 0;
    pciide_dma_clear_status(sc);
    sc->ps_dma_active = 0;
    sc->ps_dma_done = 1;
    if (sc->ps_wakeup != 0)
        sc->ps_wakeup(sc->ps_platform_cookie, &sc->ps_dma_done);
    return 1;
}

static int
pciide_dma_transfer(struct pciide_softc *sc, unsigned lba, unsigned count,
    void *data_arg, int write)
{
    unsigned char *prd;
    unsigned char command;
    size_t bytes;
    int error;

    if (!sc->ps_dma_ready || count == 0 ||
        count > PCIIDE_DMA_MAX_SECTORS)
        return EINVAL;
    bytes = (size_t)count * DISK_SECTOR_SIZE;
    if (write)
        pciide_copy(sc->ps_buffer_dma.dm_vaddr, data_arg, bytes);
    prd = (unsigned char *)sc->ps_prd_dma.dm_vaddr;
    pciide_store_le32(prd, sc->ps_buffer_dma.dm_paddr);
    pciide_store_le32(prd + 4,
        (unsigned)bytes | PCIIDE_PRD_END);
    error = dma_sync_for_device(&sc->ps_prd_dma, 0, PCIIDE_PRD_BYTES,
        DMA_TO_DEVICE);
    if (error == 0)
        error = dma_sync_for_device(&sc->ps_buffer_dma, 0, bytes,
            write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
    if (error != 0)
        return error;

    pciide_control_write(sc, 0);
    pciide_dma_stop(sc);
    pciide_dma_clear_status(sc);
    pci_resource_write32(&sc->ps_bus_master, PCIIDE_BM_PRDT,
        sc->ps_prd_dma.dm_paddr);
    command = write ? 0 : PCIIDE_BM_COMMAND_READ;
    pci_resource_write8(&sc->ps_bus_master, PCIIDE_BM_COMMAND, command);
    sc->ps_dma_done = 0;
    sc->ps_dma_error = 0;
    sc->ps_dma_bm_status = 0xffu;
    sc->ps_dma_ata_status = 0xffu;
    sc->ps_dma_active = 1;
    pciide_program_lba(sc, lba, count);
    pciide_command_write8(sc, ATA_REG_COMMAND,
        write ? ATA_COMMAND_WRITE_DMA : ATA_COMMAND_READ_DMA);
    pci_resource_write8(&sc->ps_bus_master, PCIIDE_BM_COMMAND,
        command | PCIIDE_BM_COMMAND_START);
    error = sc->ps_wait(sc->ps_platform_cookie, &sc->ps_dma_done,
        sc->ps_wait_ticks);
    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    if (error != 0 || !sc->ps_dma_done || sc->ps_dma_error) {
        if (!sc->ps_dma_done) {
            sc->ps_dma_bm_status = pci_resource_read8(
                &sc->ps_bus_master, PCIIDE_BM_STATUS);
            sc->ps_dma_ata_status = pciide_command_read8(sc,
                ATA_REG_STATUS);
        }
        printf("ata0: DMA failure wait=%d done=%u dma-error=%u "
            "lba=%u sectors=%u\n", error, sc->ps_dma_done,
            sc->ps_dma_error, lba, count);
        printf("ata0: DMA registers command=%x status=%x ata=%x "
            "prdt=%x\n",
            pci_resource_read8(&sc->ps_bus_master,
                PCIIDE_BM_COMMAND), sc->ps_dma_bm_status,
            sc->ps_dma_ata_status,
            pci_resource_read32(&sc->ps_bus_master, PCIIDE_BM_PRDT));
        printf("ata0: DMA descriptor buffer=%x bytes=%u direction=%s\n",
            sc->ps_buffer_dma.dm_paddr, (unsigned)bytes,
            write ? "write" : "read");
        printf("ata0: DMA PCI id=%x:%x interface=%x command=%x bar4=%x\n",
            sc->ps_device.pd_vendor, sc->ps_device.pd_product,
            sc->ps_device.pd_interface,
            pci_config_read32(&sc->ps_device,
                PCI_CONFIG_COMMAND_STATUS) & 0xffffu,
            pci_config_read32(&sc->ps_device, PCI_CONFIG_BAR(4)));
        if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_VIA)
            printf("ata0: DMA VIA ideconf=%x ctlmisc=%x datatim=%x "
                "misctim=%x udma=%x\n",
                pci_config_read32(&sc->ps_device, VIA_IDECONF),
                pci_config_read32(&sc->ps_device, VIA_CTLMISC),
                pci_config_read32(&sc->ps_device, VIA_DATATIM),
                pci_config_read32(&sc->ps_device, VIA_MISCTIM),
                pci_config_read32(&sc->ps_device, VIA_UDMA));
        pciide_disable_dma(sc);
        return EIO;
    }
    error = dma_sync_for_cpu(&sc->ps_buffer_dma, 0, bytes,
        write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
    if (error != 0)
        return error;
    if (!write)
        pciide_copy(data_arg, sc->ps_buffer_dma.dm_vaddr, bytes);
    return 0;
}

static int
pciide_pio_read(struct pciide_softc *sc, unsigned lba, unsigned count,
    unsigned char *data)
{
    unsigned done;
    int error;

    for (done = 0; done < count; ++done) {
        error = pciide_pio_read_one(sc, lba + done,
            data + done * DISK_SECTOR_SIZE);
        if (error != 0)
            return error;
    }
    return 0;
}

static int
pciide_pio_write(struct pciide_softc *sc, unsigned lba, unsigned count,
    const unsigned char *data)
{
    unsigned done;
    int error;

    for (done = 0; done < count; ++done) {
        error = pciide_pio_write_one(sc, lba + done,
            data + done * DISK_SECTOR_SIZE);
        if (error != 0)
            return error;
    }
    return 0;
}

static int
pciide_backend_read(void *arg, disk_sector_t lba, unsigned count,
    void *data_arg)
{
    struct pciide_softc *sc;
    unsigned char *data;
    unsigned chunk;
    unsigned done;
    int error;

    sc = (struct pciide_softc *)arg;
    if (sc == 0 || !sc->ps_present)
        return ENXIO;
    if (data_arg == 0 || count == 0 || lba >= sc->ps_sector_count ||
        (disk_sector_t)count > sc->ps_sector_count - lba)
        return EINVAL;
    data = (unsigned char *)data_arg;
    if (sc->ps_mode == PCIIDE_TRANSFER_PIO)
        return pciide_pio_read(sc, (unsigned)lba, count, data);
    done = 0;
    while (done < count) {
        chunk = count - done;
        if (chunk > PCIIDE_DMA_MAX_SECTORS)
            chunk = PCIIDE_DMA_MAX_SECTORS;
        error = pciide_dma_transfer(sc, (unsigned)lba + done, chunk,
            data + done * DISK_SECTOR_SIZE, 0);
        if (error != 0 && sc->ps_mode == PCIIDE_TRANSFER_PIO)
            return pciide_pio_read(sc, (unsigned)lba + done,
                count - done, data + done * DISK_SECTOR_SIZE);
        if (error != 0)
            return error;
        done += chunk;
    }
    return 0;
}

static int
pciide_backend_write(void *arg, disk_sector_t lba, unsigned count,
    const void *data_arg)
{
    struct pciide_softc *sc;
    const unsigned char *data;
    unsigned chunk;
    unsigned done;
    int error;

    sc = (struct pciide_softc *)arg;
    if (sc == 0 || !sc->ps_present)
        return ENXIO;
    if (data_arg == 0 || count == 0 || lba >= sc->ps_sector_count ||
        (disk_sector_t)count > sc->ps_sector_count - lba)
        return EINVAL;
    data = (const unsigned char *)data_arg;
    if (sc->ps_mode == PCIIDE_TRANSFER_PIO)
        return pciide_pio_write(sc, (unsigned)lba, count, data);
    done = 0;
    while (done < count) {
        chunk = count - done;
        if (chunk > PCIIDE_DMA_MAX_SECTORS)
            chunk = PCIIDE_DMA_MAX_SECTORS;
        error = pciide_dma_transfer(sc, (unsigned)lba + done, chunk,
            (void *)(data + done * DISK_SECTOR_SIZE), 1);
        if (error != 0 && sc->ps_mode == PCIIDE_TRANSFER_PIO)
            return pciide_pio_write(sc, (unsigned)lba + done,
                count - done, data + done * DISK_SECTOR_SIZE);
        if (error != 0)
            return error;
        done += chunk;
    }
    return 0;
}

static int
pciide_backend_flush(void *arg)
{
    struct pciide_softc *sc;
    int error;

    sc = (struct pciide_softc *)arg;
    if (sc == 0 || !sc->ps_present)
        return ENXIO;
    if (!sc->ps_flush_supported)
        return 0;
    pciide_control_write(sc, ATA_CONTROL_INTERRUPT_DISABLE);
    error = pciide_wait_not_busy(sc, 0);
    if (error != 0)
        return error;
    pciide_select_lba(sc, 0);
    pciide_command_write8(sc, ATA_REG_COMMAND, ATA_COMMAND_FLUSH_CACHE);
    return pciide_wait_not_busy(sc, 1);
}

static int
pciide_backend_present(void *arg)
{
    struct pciide_softc *sc;

    sc = (struct pciide_softc *)arg;
    return sc != 0 && sc->ps_present;
}

static void
pciide_dma_release(struct pciide_softc *sc)
{
    if (sc->ps_buffer_dma.dm_cookie != 0)
        (void)dma_free(&sc->ps_buffer_dma);
    if (sc->ps_prd_dma.dm_cookie != 0)
        (void)dma_free(&sc->ps_prd_dma);
    sc->ps_dma_ready = 0;
}

static int
pciide_dma_attach(struct pciide_softc *sc,
    const struct pciide_attach_args *args)
{
    unsigned char status;
    unsigned mode;
    unsigned pio_mode;
    int error;

    if ((sc->ps_device.pd_interface & PCIIDE_INTERFACE_BUS_MASTER) == 0 ||
        !pciide_controller_supported(sc) || !dma_pool_ready() ||
        args->pa_irq_establish == 0 || args->pa_wait == 0 ||
        args->pa_wakeup == 0 || args->pa_wait_ticks == 0)
        return EOPNOTSUPP;
    error = pciide_choose_mwdma(sc, &mode);
    if (error != 0)
        return error;
    pio_mode = mode == 0 ? 0 : mode + 2u;
    if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_VIA)
        pciide_via_pair_mwdma(sc, &mode, &pio_mode);
    error = pci_map_bar(&sc->ps_device, PCIIDE_BUS_MASTER_BAR,
        PCIIDE_BUS_MASTER_BYTES, &sc->ps_bus_master);
    if (error != 0)
        return error;
    error = dma_alloc(&sc->ps_prd_dma, PCIIDE_PRD_BYTES,
        PCIIDE_PRD_ALIGNMENT,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0)
        return error;
    error = dma_alloc(&sc->ps_buffer_dma, PCIIDE_DMA_BUFFER_BYTES,
        PCIIDE_DMA_BUFFER_ALIGNMENT,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0) {
        pciide_dma_release(sc);
        return error;
    }
    error = pci_device_enable(&sc->ps_device,
        PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    if (error != 0) {
        pciide_dma_release(sc);
        return error;
    }
    if (sc->ps_device.pd_vendor == PCIIDE_VENDOR_INTEL)
        pciide_configure_piix(sc, 1, mode);
    else
        pciide_configure_via(sc, pio_mode);
    error = pciide_set_transfer_mode(sc,
        (unsigned char)(ATA_TRANSFER_MWDMA | mode));
    if (error != 0) {
        pciide_dma_release(sc);
        pciide_configure_pio(sc);
        return error;
    }
    status = pci_resource_read8(&sc->ps_bus_master, PCIIDE_BM_STATUS);
    pci_resource_write8(&sc->ps_bus_master, PCIIDE_BM_STATUS,
        (unsigned char)((status & 0x60u) |
        PCIIDE_BM_STATUS_DRIVE0_DMA | PCIIDE_BM_STATUS_ERROR |
        PCIIDE_BM_STATUS_INTERRUPT));
    error = args->pa_irq_establish(args->pa_platform_cookie,
        args->pa_irq, pciide_interrupt, sc);
    if (error != 0) {
        pciide_dma_release(sc);
        pciide_configure_pio(sc);
        return error;
    }
    sc->ps_mwdma_mode = mode;
    sc->ps_pio_mode = pio_mode;
    sc->ps_irq = args->pa_irq;
    sc->ps_wait_ticks = args->pa_wait_ticks;
    sc->ps_wait = args->pa_wait;
    sc->ps_wakeup = args->pa_wakeup;
    sc->ps_platform_cookie = args->pa_platform_cookie;
    sc->ps_dma_ready = 1;
    sc->ps_mode = PCIIDE_TRANSFER_DMA;
    return 0;
}

int
pciide_attach(struct pciide_softc *sc, const struct pciide_attach_args *args)
{
    int error;

    if (sc == 0 || args == 0 || args->pa_device == 0 ||
        args->pa_device->pd_class != PCI_CLASS_MASS_STORAGE ||
        args->pa_device->pd_subclass != PCI_SUBCLASS_IDE ||
        args->pa_command_port == 0 || args->pa_control_port == 0 ||
        args->pa_irq == 0 || args->pa_policy > PCIIDE_MODE_DMA)
        return EINVAL;
    if ((args->pa_device->pd_interface &
        PCIIDE_INTERFACE_PRIMARY_NATIVE) != 0)
        return EOPNOTSUPP;
    pciide_zero(sc, sizeof(*sc));
    sc->ps_device = *args->pa_device;
    if (args->pa_isa_device != 0)
        sc->ps_isa_device = *args->pa_isa_device;
    sc->ps_policy = args->pa_policy;
    error = pci_device_enable(&sc->ps_device, PCI_COMMAND_IO);
    if (error != 0)
        return error;
    error = pci_map_fixed_resource(&sc->ps_device, PCI_RESOURCE_IO,
        args->pa_command_port, 8, &sc->ps_command);
    if (error != 0)
        return error;
    error = pci_map_fixed_resource(&sc->ps_device, PCI_RESOURCE_IO,
        args->pa_control_port, 1, &sc->ps_control);
    if (error != 0)
        return error;
    error = pciide_identify(sc);
    if (error != 0)
        return error;
    if ((sc->ps_identify[ATA_IDENTIFY_CAPABILITIES] &
        ATA_CAPABILITY_LBA) == 0)
        return EOPNOTSUPP;
    sc->ps_sector_count =
        (disk_sector_t)sc->ps_identify[ATA_IDENTIFY_LBA28_LOW] |
        ((disk_sector_t)sc->ps_identify[ATA_IDENTIFY_LBA28_HIGH] << 16);
    if (sc->ps_sector_count == 0)
        return ENXIO;
    sc->ps_flush_supported =
        (sc->ps_identify[ATA_IDENTIFY_COMMAND_SET_2] &
        ATA_FLUSH_CACHE_SUPPORTED) != 0;
    sc->ps_present = 1;
    sc->ps_mode = PCIIDE_TRANSFER_PIO;
    pciide_print_model(sc);
    if (args->pa_policy != PCIIDE_MODE_PIO) {
        error = pciide_dma_attach(sc, args);
        if (error != 0 && args->pa_policy == PCIIDE_MODE_DMA) {
            sc->ps_present = 0;
            return error;
        }
        if (error != 0)
            pciide_configure_pio(sc);
    } else
        pciide_configure_pio(sc);
    sc->ps_disk_ops.dbo_read = pciide_backend_read;
    sc->ps_disk_ops.dbo_write = pciide_backend_write;
    sc->ps_disk_ops.dbo_flush = pciide_backend_flush;
    sc->ps_disk_ops.dbo_present = pciide_backend_present;
    sc->ps_attached = 1;
    if (sc->ps_mode == PCIIDE_TRANSFER_DMA)
        printf("ata0: mode=mwdma%u bus-master irq=%u pio-timing=%u "
            "identify-mwdma=%x identify-pio=%x\n",
            sc->ps_mwdma_mode, sc->ps_irq, sc->ps_pio_mode,
            sc->ps_identify[ATA_IDENTIFY_MWDMA],
            sc->ps_identify[ATA_IDENTIFY_PIO]);
    else if (args->pa_policy == PCIIDE_MODE_PIO)
        printf("ata0: mode=pio policy=forced\n");
    else
        printf("ata0: mode=pio fallback=capability\n");
    return 0;
}

const struct disk_backend_ops *
pciide_disk_ops(struct pciide_softc *sc)
{
    return sc != 0 && sc->ps_attached ? &sc->ps_disk_ops : 0;
}

disk_sector_t
pciide_sector_count(const struct pciide_softc *sc)
{
    return sc != 0 && sc->ps_attached ? sc->ps_sector_count : 0;
}

enum pciide_transfer_mode
pciide_transfer_mode(const struct pciide_softc *sc)
{
    return sc != 0 ? sc->ps_mode : PCIIDE_TRANSFER_PIO;
}

unsigned
pciide_mwdma_mode(const struct pciide_softc *sc)
{
    return sc != 0 ? sc->ps_mwdma_mode : 0;
}
