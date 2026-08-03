#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/dma.h>
#include <sys/hw_inventory_provider.h>
#include <pci/pci.h>
#include <pci/pciide.h>

#define FAKE_CONFIG_WORDS          64u
#define FAKE_DISK_SECTORS          512u
#define FAKE_DMA_BYTES             (192u * 1024u)
#define FAKE_DMA_ADDRESS_BASE      0x01000000u

#define FAKE_HANDLE_COMMAND        1u
#define FAKE_HANDLE_CONTROL        2u
#define FAKE_HANDLE_BUS_MASTER     3u

#define ATA_REG_DATA               0u
#define ATA_REG_FEATURES           1u
#define ATA_REG_SECTOR_COUNT       2u
#define ATA_REG_LBA_LOW            3u
#define ATA_REG_LBA_MID            4u
#define ATA_REG_LBA_HIGH           5u
#define ATA_REG_DRIVE              6u
#define ATA_REG_STATUS             7u
#define ATA_REG_COMMAND            7u

#define ATA_STATUS_ERROR           0x01u
#define ATA_STATUS_DATA_REQUEST    0x08u
#define ATA_STATUS_READY           0x40u
#define ATA_STATUS_BUSY            0x80u

#define ATA_COMMAND_READ_SECTORS   0x20u
#define ATA_COMMAND_WRITE_SECTORS  0x30u
#define ATA_COMMAND_READ_DMA       0xc8u
#define ATA_COMMAND_WRITE_DMA      0xcau
#define ATA_COMMAND_FLUSH_CACHE    0xe7u
#define ATA_COMMAND_IDENTIFY       0xecu
#define ATA_COMMAND_SET_FEATURES   0xefu

#define ATA_CONTROL_RESET          0x04u

#define BM_COMMAND                 0u
#define BM_STATUS                  2u
#define BM_PRDT                    4u
#define BM_COMMAND_START           0x01u
#define BM_STATUS_ERROR            0x02u
#define BM_STATUS_INTERRUPT        0x04u
#define BM_STATUS_DRIVE0_DMA       0x20u

#define PIIX_IDETIM                0x40u
#define PIIX_MWDMA2_TIMING         0xa309u
#define VIA_IDECONF                0x40u
#define VIA_DATATIM                0x48u
#define VIA_UDMA                   0x50u
#define VIA_IDECONF_PRIMARY_ENABLE 0x00000002u
#define VIA_MWDMA2_TIMING          0x20000000u
#define VIA_MWDMA1_TIMING          0x22000000u
#define VIA_MWDMA0_TIMING          0xa8000000u
#define VIA_BIOS_UDMA66_TIMING     0xe0080000u
#define VIA_UDMA4_TIMING           0xc0080000u
#define VIA_UDMA66_MODE2_TIMING    0xc2080000u
#define VIA_UDMA33_MODE2_TIMING    0xc0000000u

#define CHECK(expression) do {                                         \
    if (!(expression)) {                                               \
        fprintf(stderr, "%s:%d: check failed: %s\n",                \
            __FILE__, __LINE__, #expression);                          \
        exit(1);                                                       \
    }                                                                  \
} while (0)

struct fake_ata {
    unsigned config[FAKE_CONFIG_WORDS];
    unsigned char bar_probe[6];
    unsigned char task[8];
    unsigned char bus_master[16];
    unsigned short identify[256];
    unsigned char disk[FAKE_DISK_SECTORS * DISK_SECTOR_SIZE];
    unsigned data_word;
    unsigned current_lba;
    unsigned current_count;
    unsigned command;
    unsigned flushes;
    unsigned irq;
    unsigned waits;
    unsigned wakeups;
    unsigned inject_dma_error;
    unsigned inject_dma_timeout;
    unsigned inject_early_ata_irq;
    unsigned transfer_mode;
    unsigned dma_starts;
    unsigned last_dma_bytes;
    unsigned last_dma_count;
    unsigned last_dma_descriptors;
    unsigned last_dma_lba;
    pci_interrupt_handler_t handler;
    void *handler_arg;
};

static unsigned char fake_dma[FAKE_DMA_BYTES]
    __attribute__((aligned(256)));

static dma_addr_t
fake_dma_address(void)
{
    return (dma_addr_t)(FAKE_DMA_ADDRESS_BASE |
        ((uintptr_t)fake_dma & 0xffffu));
}

void
hw_inventory_register_pci(hw_pci_inventory_provider_t provider)
{
    (void)provider;
}

static unsigned
fake_load32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void
fake_store32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static unsigned
fake_task_lba(const struct fake_ata *ata)
{
    return (unsigned)ata->task[ATA_REG_LBA_LOW] |
        ((unsigned)ata->task[ATA_REG_LBA_MID] << 8) |
        ((unsigned)ata->task[ATA_REG_LBA_HIGH] << 16) |
        ((unsigned)(ata->task[ATA_REG_DRIVE] & 0x0fu) << 24);
}

static int
fake_present(void *cookie)
{
    (void)cookie;
    return 1;
}

static unsigned
fake_config_read32(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset)
{
    struct fake_ata *ata;
    unsigned bar;

    ata = cookie;
    if (bus != 0 || device != 1 || function != 1)
        return 0xffffffffu;
    if (offset >= PCI_CONFIG_BAR(0) && offset < PCI_CONFIG_BAR(6)) {
        bar = (offset - PCI_CONFIG_BAR(0)) / 4u;
        if (ata->bar_probe[bar])
            return bar == 4u ? 0xfffffff1u : 0;
    }
    return ata->config[(offset & 0xfcu) / 4u];
}

static unsigned char
fake_config_read8(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset)
{
    return (unsigned char)(fake_config_read32(cookie, bus, device,
        function, offset) >> ((offset & 3u) * 8u));
}

static unsigned short
fake_config_read16(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset)
{
    return (unsigned short)(fake_config_read32(cookie, bus, device,
        function, offset) >> ((offset & 2u) * 8u));
}

static void
fake_config_write32(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset, unsigned value)
{
    struct fake_ata *ata;
    unsigned bar;

    ata = cookie;
    if (bus != 0 || device != 1 || function != 1)
        return;
    if (offset >= PCI_CONFIG_BAR(0) && offset < PCI_CONFIG_BAR(6)) {
        bar = (offset - PCI_CONFIG_BAR(0)) / 4u;
        if (value == 0xffffffffu) {
            ata->bar_probe[bar] = 1;
            return;
        }
        ata->bar_probe[bar] = 0;
    }
    ata->config[(offset & 0xfcu) / 4u] = value;
}

static void
fake_config_write8(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset, unsigned char value)
{
    struct fake_ata *ata;
    unsigned shift;
    unsigned *word;

    ata = cookie;
    if (bus != 0 || device != 1 || function != 1)
        return;
    shift = (offset & 3u) * 8u;
    word = &ata->config[(offset & 0xfcu) / 4u];
    *word = (*word & ~(0xffu << shift)) | ((unsigned)value << shift);
}

static void
fake_config_write16(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset, unsigned short value)
{
    struct fake_ata *ata;
    unsigned shift;
    unsigned *word;

    ata = cookie;
    if (bus != 0 || device != 1 || function != 1)
        return;
    shift = (offset & 2u) * 8u;
    word = &ata->config[(offset & 0xfcu) / 4u];
    *word = (*word & ~(0xffffu << shift)) | ((unsigned)value << shift);
}

static int
fake_map_resource(void *cookie, enum pci_resource_type type,
    unsigned long long address, size_t size, u_long *handle)
{
    (void)cookie;
    if (type != PCI_RESOURCE_IO || handle == 0 || size == 0)
        return EINVAL;
    if (address == PCIIDE_PRIMARY_COMMAND_PORT)
        *handle = FAKE_HANDLE_COMMAND;
    else if (address == PCIIDE_PRIMARY_CONTROL_PORT)
        *handle = FAKE_HANDLE_CONTROL;
    else if (address == 0xc000u)
        *handle = FAKE_HANDLE_BUS_MASTER;
    else
        return ENXIO;
    return 0;
}

static unsigned char
fake_resource_read8(void *cookie, enum pci_resource_type type, u_long handle,
    size_t offset)
{
    struct fake_ata *ata;

    (void)type;
    ata = cookie;
    if (handle == FAKE_HANDLE_COMMAND)
        return ata->task[offset];
    if (handle == FAKE_HANDLE_CONTROL)
        return 0;
    if (handle == FAKE_HANDLE_BUS_MASTER)
        return ata->bus_master[offset];
    return 0xffu;
}

static unsigned short
fake_resource_read16(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset)
{
    struct fake_ata *ata;
    unsigned byte_offset;
    unsigned short value;

    (void)type;
    (void)offset;
    ata = cookie;
    if (handle != FAKE_HANDLE_COMMAND)
        return 0xffffu;
    if (ata->command == ATA_COMMAND_IDENTIFY)
        value = ata->identify[ata->data_word];
    else {
        byte_offset = ata->current_lba * DISK_SECTOR_SIZE +
            ata->data_word * 2u;
        value = (unsigned short)((unsigned)ata->disk[byte_offset] |
            ((unsigned)ata->disk[byte_offset + 1u] << 8));
    }
    if (++ata->data_word == DISK_SECTOR_SIZE / 2u)
        ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
    return value;
}

static unsigned
fake_resource_read32(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset)
{
    struct fake_ata *ata;

    (void)type;
    ata = cookie;
    if (handle != FAKE_HANDLE_BUS_MASTER)
        return 0xffffffffu;
    return fake_load32(&ata->bus_master[offset]);
}

static void
fake_dma_start(struct fake_ata *ata)
{
    unsigned char *prd;
    unsigned char *buffer;
    unsigned buffer_address;
    unsigned byte_count;
    unsigned descriptor;
    unsigned flags;
    unsigned prd_address;
    unsigned total_bytes;
    size_t dma_offset;
    size_t disk_offset;

    prd_address = fake_load32(&ata->bus_master[BM_PRDT]);
    CHECK(prd_address >= fake_dma_address());
    dma_offset = prd_address - fake_dma_address();
    CHECK(dma_offset + 16u <= sizeof(fake_dma));
    prd = fake_dma + dma_offset;
    ++ata->dma_starts;
    ata->last_dma_count = ata->current_count;
    ata->last_dma_lba = ata->current_lba;
    disk_offset = (size_t)ata->current_lba * DISK_SECTOR_SIZE;
    total_bytes = 0;
    descriptor = 0;
    do {
        CHECK(descriptor < 2u);
        buffer_address = fake_load32(prd + descriptor * 8u);
        flags = fake_load32(prd + descriptor * 8u + 4u);
        byte_count = flags & 0xffffu;
        if (byte_count == 0)
            byte_count = 0x10000u;
        CHECK(buffer_address >= fake_dma_address());
        dma_offset = buffer_address - fake_dma_address();
        CHECK(dma_offset + byte_count <= sizeof(fake_dma));
        CHECK(disk_offset + total_bytes + byte_count <=
            sizeof(ata->disk));
        if (!ata->inject_dma_timeout) {
            buffer = fake_dma + dma_offset;
            if (ata->command == ATA_COMMAND_READ_DMA)
                memcpy(buffer, ata->disk + disk_offset + total_bytes,
                    byte_count);
            else {
                CHECK(ata->command == ATA_COMMAND_WRITE_DMA);
                memcpy(ata->disk + disk_offset + total_bytes, buffer,
                    byte_count);
            }
        }
        total_bytes += byte_count;
        ++descriptor;
    } while ((flags & 0x80000000u) == 0);
    ata->last_dma_bytes = total_bytes;
    ata->last_dma_descriptors = descriptor;
    if (ata->inject_dma_timeout) {
        ata->inject_dma_timeout = 0;
        ata->task[ATA_REG_STATUS] = ATA_STATUS_BUSY;
        return;
    }
    if (ata->inject_early_ata_irq) {
        ata->inject_early_ata_irq = 0;
        CHECK(ata->handler != 0);
        CHECK(ata->handler(ata->handler_arg) == 1);
        return;
    }
    ata->bus_master[BM_STATUS] |= BM_STATUS_INTERRUPT;
    if (ata->inject_dma_error) {
        ata->bus_master[BM_STATUS] |= BM_STATUS_ERROR;
        ata->inject_dma_error = 0;
    }
    CHECK(ata->handler != 0);
    CHECK(ata->handler(ata->handler_arg) == 1);
}

static void
fake_resource_write8(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned char value)
{
    struct fake_ata *ata;

    (void)type;
    ata = cookie;
    if (handle == FAKE_HANDLE_CONTROL) {
        if (value & ATA_CONTROL_RESET)
            ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
        return;
    }
    if (handle == FAKE_HANDLE_BUS_MASTER) {
        if (offset == BM_STATUS) {
            ata->bus_master[offset] &=
                (unsigned char)~(value &
                (BM_STATUS_ERROR | BM_STATUS_INTERRUPT));
            ata->bus_master[offset] |= BM_STATUS_DRIVE0_DMA;
            return;
        }
        ata->bus_master[offset] = value;
        if (offset == BM_COMMAND && (value & BM_COMMAND_START))
            fake_dma_start(ata);
        return;
    }
    CHECK(handle == FAKE_HANDLE_COMMAND);
    ata->task[offset] = value;
    if (offset != ATA_REG_COMMAND)
        return;
    ata->command = value;
    ata->current_lba = fake_task_lba(ata);
    ata->current_count = ata->task[ATA_REG_SECTOR_COUNT];
    if (ata->current_count == 0 &&
        (value == ATA_COMMAND_READ_DMA || value == ATA_COMMAND_WRITE_DMA))
        ata->current_count = 256u;
    ata->data_word = 0;
    if (value == ATA_COMMAND_IDENTIFY || value == ATA_COMMAND_READ_SECTORS ||
        value == ATA_COMMAND_WRITE_SECTORS)
        ata->task[ATA_REG_STATUS] =
            ATA_STATUS_READY | ATA_STATUS_DATA_REQUEST;
    else if (value == ATA_COMMAND_FLUSH_CACHE) {
        ++ata->flushes;
        ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
    } else if (value == ATA_COMMAND_SET_FEATURES ||
        value == ATA_COMMAND_READ_DMA || value == ATA_COMMAND_WRITE_DMA) {
        if (value == ATA_COMMAND_SET_FEATURES)
            ata->transfer_mode = ata->task[ATA_REG_SECTOR_COUNT];
        ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
    } else
        ata->task[ATA_REG_STATUS] = ATA_STATUS_ERROR;
}

static void
fake_resource_write16(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned short value)
{
    struct fake_ata *ata;
    unsigned byte_offset;

    (void)type;
    (void)offset;
    ata = cookie;
    CHECK(handle == FAKE_HANDLE_COMMAND);
    CHECK(ata->command == ATA_COMMAND_WRITE_SECTORS);
    byte_offset = ata->current_lba * DISK_SECTOR_SIZE +
        ata->data_word * 2u;
    ata->disk[byte_offset] = (unsigned char)value;
    ata->disk[byte_offset + 1u] = (unsigned char)(value >> 8);
    if (++ata->data_word == DISK_SECTOR_SIZE / 2u)
        ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
}

static void
fake_resource_write32(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned value)
{
    struct fake_ata *ata;

    (void)type;
    ata = cookie;
    CHECK(handle == FAKE_HANDLE_BUS_MASTER);
    fake_store32(&ata->bus_master[offset], value);
}

static int
fake_interrupt_establish(void *cookie, unsigned interrupt,
    pci_interrupt_handler_t handler, void *arg)
{
    struct fake_ata *ata;

    ata = cookie;
    ata->irq = interrupt;
    ata->handler = handler;
    ata->handler_arg = arg;
    return 0;
}

static void
fake_delay(void *cookie, unsigned microseconds)
{
    (void)cookie;
    (void)microseconds;
}

static int
fake_wait(void *cookie, volatile unsigned *done, unsigned ticks)
{
    struct fake_ata *ata;

    ata = cookie;
    ++ata->waits;
    CHECK(ticks != 0);
    return *done ? 0 : ETIMEDOUT;
}

static void
fake_wakeup(void *cookie, volatile unsigned *done)
{
    struct fake_ata *ata;

    ata = cookie;
    CHECK(*done != 0);
    ++ata->wakeups;
}

static const struct pci_bus_ops fake_ops = {
    fake_present,
    fake_config_read8,
    fake_config_read16,
    fake_config_read32,
    fake_config_write8,
    fake_config_write16,
    fake_config_write32,
    fake_map_resource,
    fake_resource_read8,
    fake_resource_read16,
    fake_resource_read32,
    fake_resource_write8,
    fake_resource_write16,
    fake_resource_write32,
    fake_interrupt_establish,
    fake_delay,
};

static void
fake_init(struct fake_ata *ata, int dma_capable)
{
    static const char model[] = "REBSD PCIIDE TEST DISK";
    unsigned i;

    memset(ata, 0, sizeof(*ata));
    ata->config[PCI_CONFIG_COMMAND_STATUS / 4u] = 0;
    ata->config[PCI_CONFIG_BAR(4) / 4u] = 0xc001u;
    ata->task[ATA_REG_STATUS] = ATA_STATUS_READY;
    ata->bus_master[BM_STATUS] = BM_STATUS_DRIVE0_DMA;
    ata->identify[49] = 0x0200u | (dma_capable ? 0x0100u : 0);
    ata->identify[53] = dma_capable ? 0x0002u : 0;
    ata->identify[60] = FAKE_DISK_SECTORS;
    ata->identify[61] = 0;
    ata->identify[63] = dma_capable ? 0x0007u : 0;
    ata->identify[64] = dma_capable ? 0x0003u : 0;
    ata->identify[83] = 0x1000u;
    for (i = 0; i < 20u; ++i) {
        unsigned first;
        unsigned second;

        first = i * 2u < sizeof(model) - 1u ? model[i * 2u] : ' ';
        second = i * 2u + 1u < sizeof(model) - 1u ?
            model[i * 2u + 1u] : ' ';
        ata->identify[27u + i] = (unsigned short)((first << 8) | second);
    }
    for (i = 0; i < sizeof(ata->disk); ++i)
        ata->disk[i] = (unsigned char)(i * 17u + 3u);
}

static void
test_mode(enum pciide_mode_policy policy, int dma_capable, int failure,
    int via, unsigned pio_modes, unsigned expected_mwdma,
    unsigned via_udma_case)
{
    struct fake_ata ata;
    struct pci_bus bus;
    struct pci_device device;
    struct pci_device isa;
    struct pciide_attach_args args;
    struct pciide_softc sc;
    const struct disk_backend_ops *ops;
    unsigned char original[DISK_SECTOR_SIZE];
    unsigned char data[DISK_SECTOR_SIZE];
    unsigned char dma_data[PCIIDE_DMA_BUFFER_BYTES];
    unsigned char replacement[DISK_SECTOR_SIZE];
    unsigned i;
    int error;

    fake_init(&ata, dma_capable);
    ata.identify[64] = (unsigned short)pio_modes;
    if (via) {
        ata.config[VIA_UDMA / 4u] = VIA_BIOS_UDMA66_TIMING;
        if (via_udma_case != 0) {
            ata.identify[53] |= 0x0004u;
            ata.identify[88] = 0x001fu;
            if (via_udma_case != 1u)
                ata.config[VIA_UDMA / 4u] &= ~0x00080000u;
        }
    }
    memset(&bus, 0, sizeof(bus));
    bus.pb_ops = &fake_ops;
    bus.pb_cookie = &ata;
    bus.pb_attached = 1;
    memset(&device, 0, sizeof(device));
    device.pd_bus = &bus;
    device.pd_device = 1;
    device.pd_function = 1;
    device.pd_class = PCI_CLASS_MASS_STORAGE;
    device.pd_subclass = PCI_SUBCLASS_IDE;
    device.pd_interface = 0x80u;
    device.pd_vendor = via ? 0x1106u : 0x8086u;
    device.pd_product = via ? 0x0571u : 0x7010u;
    memset(&isa, 0, sizeof(isa));
    isa.pd_vendor = 0x1106u;
    isa.pd_product = 0x0596u;
    isa.pd_revision = via_udma_case == 3u ? 0x0fu :
        (via_udma_case != 0 ? 0x12u : 0);
    memset(&args, 0, sizeof(args));
    args.pa_device = &device;
    args.pa_isa_device = via ? &isa : 0;
    args.pa_policy = policy;
    args.pa_command_port = PCIIDE_PRIMARY_COMMAND_PORT;
    args.pa_control_port = PCIIDE_PRIMARY_CONTROL_PORT;
    args.pa_irq = PCIIDE_PRIMARY_IRQ;
    args.pa_wait_ticks = 100;
    args.pa_irq_establish = fake_interrupt_establish;
    args.pa_wait = fake_wait;
    args.pa_wakeup = fake_wakeup;
    args.pa_platform_cookie = &ata;
    CHECK(dma_pool_init(fake_dma, fake_dma_address(), sizeof(fake_dma),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0) == 0);
    error = pciide_attach(&sc, &args);
    if (error != 0)
        fprintf(stderr, "pciide attach error: %d\n", error);
    CHECK(error == 0);
    ops = pciide_disk_ops(&sc);
    CHECK(ops != 0 && pciide_sector_count(&sc) == FAKE_DISK_SECTORS);
    if (policy == PCIIDE_MODE_DMA ||
        (policy == PCIIDE_MODE_AUTO && dma_capable)) {
        unsigned expected_mode;

        expected_mode = via_udma_case == 1u ? 4u :
            (via_udma_case != 0 ? 2u : expected_mwdma);
        CHECK(pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_DMA);
        CHECK(pciide_dma_protocol(&sc) ==
            (via_udma_case != 0 ? PCIIDE_DMA_UDMA : PCIIDE_DMA_MWDMA));
        CHECK(pciide_dma_mode(&sc) == expected_mode);
        CHECK(ata.transfer_mode ==
            ((via_udma_case != 0 ? 0x40u : 0x20u) | expected_mode));
        CHECK(ata.irq == PCIIDE_PRIMARY_IRQ);
    } else
        CHECK(pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_PIO);
    if (via) {
        CHECK((ata.config[VIA_IDECONF / 4u] &
            VIA_IDECONF_PRIMARY_ENABLE) != 0);
        if (via_udma_case == 1u)
            CHECK(ata.config[VIA_UDMA / 4u] == VIA_UDMA4_TIMING);
        else if (via_udma_case == 2u)
            CHECK(ata.config[VIA_UDMA / 4u] ==
                VIA_UDMA66_MODE2_TIMING);
        else if (via_udma_case == 3u)
            CHECK(ata.config[VIA_UDMA / 4u] ==
                VIA_UDMA33_MODE2_TIMING);
        else if (expected_mwdma == 2u)
            CHECK((ata.config[VIA_DATATIM / 4u] & 0xff000000u) ==
                VIA_MWDMA2_TIMING);
        else if (expected_mwdma == 1u)
            CHECK((ata.config[VIA_DATATIM / 4u] & 0xff000000u) ==
                VIA_MWDMA1_TIMING);
        else
            CHECK((ata.config[VIA_DATATIM / 4u] & 0xff000000u) ==
                VIA_MWDMA0_TIMING);
        if (via_udma_case == 0)
            CHECK((ata.config[VIA_UDMA / 4u] & 0xff000000u) == 0);
    } else if (pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_DMA)
        CHECK((ata.config[PIIX_IDETIM / 4u] & 0xffffu) ==
            PIIX_MWDMA2_TIMING);
    else
        CHECK((ata.config[PIIX_IDETIM / 4u] & 0xffffu) == 0x8000u);

    memcpy(original, ata.disk + 7u * DISK_SECTOR_SIZE, sizeof(original));
    if (failure == 3) {
        ata.inject_early_ata_irq = 1;
        CHECK(ops->dbo_read(&sc, 7, 1, data) == 0);
        CHECK(pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_DMA);
        CHECK(memcmp(data, original, sizeof(data)) == 0);
    } else if (failure != 0) {
        if (failure == 1)
            ata.inject_dma_error = 1;
        else
            ata.inject_dma_timeout = 1;
        CHECK(ops->dbo_read(&sc, 7, 1, data) == 0);
        CHECK(pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_PIO);
        CHECK(ata.transfer_mode == 0x08u);
        if (via)
            CHECK((ata.config[VIA_UDMA / 4u] & 0xff000000u) == 0);
        CHECK(memcmp(data, original, sizeof(data)) == 0);
    }
    CHECK(ops->dbo_read(&sc, 7, 1, data) == 0);
    CHECK(memcmp(data, original, sizeof(data)) == 0);
    if (pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_DMA && failure == 0) {
        ata.dma_starts = 0;
        CHECK(ops->dbo_read(&sc, 16, PCIIDE_DMA_MAX_SECTORS,
            dma_data) == 0);
        CHECK(ata.dma_starts == 1u);
        CHECK(ata.last_dma_lba == 16u);
        CHECK(ata.last_dma_count == PCIIDE_DMA_MAX_SECTORS);
        CHECK(ata.last_dma_bytes == PCIIDE_DMA_BUFFER_BYTES);
        CHECK(ata.last_dma_descriptors == 2u);
        CHECK(memcmp(dma_data, ata.disk + 16u * DISK_SECTOR_SIZE,
            sizeof(dma_data)) == 0);
        for (i = 0; i < sizeof(dma_data); ++i)
            dma_data[i] = (unsigned char)(0x5au ^ i);
        ata.dma_starts = 0;
        CHECK(ops->dbo_write(&sc, 256u, PCIIDE_DMA_MAX_SECTORS,
            dma_data) == 0);
        CHECK(ata.dma_starts == 1u);
        CHECK(ata.last_dma_lba == 256u);
        CHECK(ata.last_dma_count == PCIIDE_DMA_MAX_SECTORS);
        CHECK(ata.last_dma_bytes == PCIIDE_DMA_BUFFER_BYTES);
        CHECK(ata.last_dma_descriptors == 2u);
        CHECK(memcmp(ata.disk + 256u * DISK_SECTOR_SIZE, dma_data,
            sizeof(dma_data)) == 0);
    }
    for (i = 0; i < sizeof(replacement); ++i)
        replacement[i] = (unsigned char)(0xa5u ^ i);
    CHECK(ops->dbo_write(&sc, 9, 1, replacement) == 0);
    CHECK(memcmp(ata.disk + 9u * DISK_SECTOR_SIZE, replacement,
        sizeof(replacement)) == 0);
    CHECK(ops->dbo_flush(&sc) == 0 && ata.flushes == 1u);
    CHECK(ops->dbo_read(&sc, FAKE_DISK_SECTORS, 1, data) == EINVAL);
    if (pciide_transfer_mode(&sc) == PCIIDE_TRANSFER_DMA)
        CHECK(ata.waits != 0 && ata.wakeups == ata.waits);
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
            "usage: %s pio|dma|piix-dma-pio2|via-dma|via-dma-pio3|via-dma-pio2|via-udma66|via-udma66-no80|via-udma33|auto-fallback|dma-error|dma-timeout|via-dma-timeout|via-udma-timeout|dma-early-irq\n",
            argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "pio") == 0)
        test_mode(PCIIDE_MODE_PIO, 1, 0, 0, 3, 2, 0);
    else if (strcmp(argv[1], "dma") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 0, 3, 2, 0);
    else if (strcmp(argv[1], "piix-dma-pio2") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 0, 0, 2, 0);
    else if (strcmp(argv[1], "via-dma") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 3, 2, 0);
    else if (strcmp(argv[1], "via-dma-pio3") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 1, 1, 0);
    else if (strcmp(argv[1], "via-dma-pio2") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 0, 0, 0);
    else if (strcmp(argv[1], "via-udma66") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 3, 2, 1);
    else if (strcmp(argv[1], "via-udma66-no80") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 3, 2, 2);
    else if (strcmp(argv[1], "via-udma33") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 0, 1, 3, 2, 3);
    else if (strcmp(argv[1], "auto-fallback") == 0)
        test_mode(PCIIDE_MODE_AUTO, 0, 0, 0, 0, 0, 0);
    else if (strcmp(argv[1], "dma-error") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 1, 0, 3, 2, 0);
    else if (strcmp(argv[1], "dma-timeout") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 2, 0, 3, 2, 0);
    else if (strcmp(argv[1], "via-dma-timeout") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 2, 1, 3, 2, 0);
    else if (strcmp(argv[1], "via-udma-timeout") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 2, 1, 3, 2, 1);
    else if (strcmp(argv[1], "dma-early-irq") == 0)
        test_mode(PCIIDE_MODE_DMA, 1, 3, 1, 3, 2, 0);
    else
        return 2;
    printf("pciide %s test: ok\n", argv[1]);
    return 0;
}
