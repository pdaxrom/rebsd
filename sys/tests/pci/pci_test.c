#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/dma.h>
#include <sys/hw_inventory_provider.h>
#include <pci/pci.h>
#include <pci/rtl8169.h>

#define FAKE_FUNCTIONS                 2u
#define FAKE_CONFIG_WORDS              64u
#define FAKE_REGISTERS                 256u
#define FAKE_DMA_BYTES                 65536u
#define FAKE_DMA_ADDRESS               0x01000000u

#define RTL_REG_MAC0                   0x00u
#define RTL_REG_CHIP_COMMAND           0x37u
#define RTL_REG_TX_POLL                0x38u
#define RTL_REG_INTERRUPT_MASK         0x3cu
#define RTL_REG_INTERRUPT_STATUS       0x3eu
#define RTL_REG_TX_CONFIG              0x40u
#define RTL_REG_PHY_ACCESS             0x60u
#define RTL_REG_PHY_STATUS             0x6cu
#define RTL_REG_RX_MAX_SIZE            0xdau

#define RTL_COMMAND_RESET              0x10u
#define RTL_TX_POLL_NORMAL             0x40u
#define RTL_INTERRUPT_SYSTEM_ERROR     0x8000u
#define RTL_INTERRUPT_LINK_CHANGE      0x0020u
#define RTL_INTERRUPT_TX_OK            0x0004u
#define RTL_INTERRUPT_RX_ERROR         0x0002u
#define RTL_INTERRUPT_RX_OK            0x0001u
#define RTL_DESCRIPTOR_OWN             0x80000000u
#define RTL_DESCRIPTOR_FIRST           0x20000000u
#define RTL_DESCRIPTOR_LAST            0x10000000u
#define RTL_PHY_ACCESS_FLAG            0x80000000u
#define RTL_PHY_STATUS_LINK            0x02u

#define CHECK(expression) do {                                             \
    if (!(expression)) {                                                   \
        fprintf(stderr, "%s:%d: check failed: %s\n",                    \
            __FILE__, __LINE__, #expression);                              \
        exit(1);                                                           \
    }                                                                      \
} while (0)

struct fake_function {
    unsigned char ff_bus;
    unsigned char ff_device;
    unsigned char ff_function;
    unsigned ff_config[FAKE_CONFIG_WORDS];
    unsigned ff_bar_mask[6];
    unsigned char ff_bar_probe[6];
};

struct fake_pci {
    struct fake_function fp_function[FAKE_FUNCTIONS];
    unsigned char fp_registers[FAKE_REGISTERS];
    unsigned short fp_phy[32];
    pci_interrupt_handler_t fp_handler;
    void *fp_handler_arg;
    unsigned fp_irq;
    unsigned fp_delays;
    enum pci_resource_type fp_last_map_type;
    unsigned long long fp_last_map_address;
    size_t fp_last_map_size;
};

struct callback_state {
    unsigned cs_rx_count;
    unsigned cs_rx_errors;
    unsigned cs_tx_completed;
    unsigned cs_tx_errors;
    unsigned cs_link_changes;
    int cs_link;
    unsigned cs_length;
    unsigned char cs_frame[RTL8169_FRAME_BYTES];
};

static unsigned char fake_dma[FAKE_DMA_BYTES]
    __attribute__((aligned(256)));
static hw_pci_inventory_provider_t inventory_provider;

void
hw_inventory_register_pci(hw_pci_inventory_provider_t provider)
{
    inventory_provider = provider;
}

static struct fake_function *
fake_find_function(struct fake_pci *pci, unsigned char bus,
    unsigned char device, unsigned char function)
{
    unsigned i;

    for (i = 0; i < FAKE_FUNCTIONS; ++i) {
        if (pci->fp_function[i].ff_bus == bus &&
            pci->fp_function[i].ff_device == device &&
            pci->fp_function[i].ff_function == function)
            return &pci->fp_function[i];
    }
    return 0;
}

static int
fake_bar_number(unsigned char offset)
{
    if (offset < PCI_CONFIG_BAR(0) || offset >= PCI_CONFIG_BAR(6))
        return -1;
    return (offset - PCI_CONFIG_BAR(0)) / 4;
}

static unsigned
fake_config_read32(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset)
{
    struct fake_pci *pci;
    struct fake_function *fn;
    int bar;

    pci = cookie;
    fn = fake_find_function(pci, bus, device, function);
    if (fn == 0)
        return 0xffffffffu;
    bar = fake_bar_number(offset);
    if (bar >= 0 && fn->ff_bar_probe[bar])
        return fn->ff_bar_mask[bar];
    return fn->ff_config[(offset & 0xfcu) / 4u];
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
    struct fake_pci *pci;
    struct fake_function *fn;
    unsigned current;
    int bar;

    pci = cookie;
    fn = fake_find_function(pci, bus, device, function);
    if (fn == 0)
        return;
    bar = fake_bar_number(offset);
    if (bar >= 0) {
        if (value == 0xffffffffu) {
            fn->ff_bar_probe[bar] = 1;
            return;
        }
        fn->ff_bar_probe[bar] = 0;
        fn->ff_config[(offset & 0xfcu) / 4u] = value;
        return;
    }
    if ((offset & 0xfcu) == PCI_CONFIG_COMMAND_STATUS) {
        current = fn->ff_config[PCI_CONFIG_COMMAND_STATUS / 4u];
        current &= ~value & 0xffff0000u;
        current = (current & 0xffff0000u) | (value & 0xffffu);
        fn->ff_config[PCI_CONFIG_COMMAND_STATUS / 4u] = current;
    } else
        fn->ff_config[(offset & 0xfcu) / 4u] = value;
}

static void
fake_config_write8(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset, unsigned char value)
{
    struct fake_pci *pci;
    struct fake_function *fn;
    unsigned shift;
    unsigned *word;

    pci = cookie;
    fn = fake_find_function(pci, bus, device, function);
    if (fn == 0)
        return;
    shift = (offset & 3u) * 8u;
    word = &fn->ff_config[(offset & 0xfcu) / 4u];
    *word = (*word & ~(0xffu << shift)) | ((unsigned)value << shift);
}

static void
fake_config_write16(void *cookie, unsigned char bus, unsigned char device,
    unsigned char function, unsigned char offset, unsigned short value)
{
    struct fake_pci *pci;
    struct fake_function *fn;
    unsigned shift;
    unsigned *word;

    pci = cookie;
    fn = fake_find_function(pci, bus, device, function);
    if (fn == 0)
        return;
    shift = (offset & 2u) * 8u;
    word = &fn->ff_config[(offset & 0xfcu) / 4u];
    if (shift == 16u)
        *word &= ~((unsigned)value << 16);
    else
        *word = (*word & 0xffff0000u) | value;
}

static unsigned
fake_load32(const unsigned char *data, size_t offset)
{
    return (unsigned)data[offset] |
        ((unsigned)data[offset + 1u] << 8) |
        ((unsigned)data[offset + 2u] << 16) |
        ((unsigned)data[offset + 3u] << 24);
}

static unsigned short
fake_load16(const unsigned char *data, size_t offset)
{
    return (unsigned short)((unsigned)data[offset] |
        ((unsigned)data[offset + 1u] << 8));
}

static void
fake_store32(unsigned char *data, size_t offset, unsigned value)
{
    data[offset] = (unsigned char)value;
    data[offset + 1u] = (unsigned char)(value >> 8);
    data[offset + 2u] = (unsigned char)(value >> 16);
    data[offset + 3u] = (unsigned char)(value >> 24);
}

static void
fake_store16(unsigned char *data, size_t offset, unsigned short value)
{
    data[offset] = (unsigned char)value;
    data[offset + 1u] = (unsigned char)(value >> 8);
}

static int
fake_present(void *cookie)
{
    (void)cookie;
    return 1;
}

static int
fake_map_resource(void *cookie, enum pci_resource_type type,
    unsigned long long address, size_t size, u_long *handle)
{
    struct fake_pci *pci;

    pci = cookie;
    pci->fp_last_map_type = type;
    pci->fp_last_map_address = address;
    pci->fp_last_map_size = size;
    if (address == 0xf0000000ull || address == 0xc000ull) {
        *handle = (u_long)(uintptr_t)pci->fp_registers;
        return 0;
    }
    if (address == 0x1e0000000ull) {
        *handle = (u_long)(uintptr_t)pci->fp_registers;
        return 0;
    }
    return ENXIO;
}

static unsigned char
fake_resource_read8(void *cookie, enum pci_resource_type type, u_long handle,
    size_t offset)
{
    (void)cookie;
    (void)type;
    return ((unsigned char *)(uintptr_t)handle)[offset];
}

static unsigned short
fake_resource_read16(void *cookie, enum pci_resource_type type, u_long handle,
    size_t offset)
{
    (void)cookie;
    (void)type;
    return fake_load16((unsigned char *)(uintptr_t)handle, offset);
}

static unsigned
fake_resource_read32(void *cookie, enum pci_resource_type type, u_long handle,
    size_t offset)
{
    (void)cookie;
    (void)type;
    return fake_load32((unsigned char *)(uintptr_t)handle, offset);
}

static void
fake_resource_write8(void *cookie, enum pci_resource_type type, u_long handle,
    size_t offset, unsigned char value)
{
    unsigned char *registers;

    (void)cookie;
    (void)type;
    registers = (unsigned char *)(uintptr_t)handle;
    registers[offset] = value;
    if (offset == RTL_REG_CHIP_COMMAND && (value & RTL_COMMAND_RESET))
        registers[offset] = 0;
}

static void
fake_resource_write16(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned short value)
{
    unsigned char *registers;
    unsigned short status;

    (void)cookie;
    (void)type;
    registers = (unsigned char *)(uintptr_t)handle;
    if (offset == RTL_REG_INTERRUPT_STATUS) {
        status = fake_load16(registers, offset);
        fake_store16(registers, offset, status & ~value);
    } else
        fake_store16(registers, offset, value);
}

static void
fake_resource_write32(void *cookie, enum pci_resource_type type,
    u_long handle, size_t offset, unsigned value)
{
    struct fake_pci *pci;
    unsigned reg;

    (void)type;
    pci = cookie;
    if (offset == RTL_REG_PHY_ACCESS) {
        reg = (value >> 16) & 0x1fu;
        if (value & RTL_PHY_ACCESS_FLAG)
            fake_store32((unsigned char *)(uintptr_t)handle, offset,
                pci->fp_phy[reg]);
        else
            fake_store32((unsigned char *)(uintptr_t)handle, offset,
                RTL_PHY_ACCESS_FLAG | pci->fp_phy[reg]);
    } else
        fake_store32((unsigned char *)(uintptr_t)handle, offset, value);
}

static int
fake_interrupt_establish(void *cookie, unsigned irq,
    pci_interrupt_handler_t handler, void *arg)
{
    struct fake_pci *pci;

    pci = cookie;
    pci->fp_irq = irq;
    pci->fp_handler = handler;
    pci->fp_handler_arg = arg;
    return 0;
}

static void
fake_delay(void *cookie, unsigned microseconds)
{
    struct fake_pci *pci;

    pci = cookie;
    pci->fp_delays += microseconds;
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
fake_init(struct fake_pci *pci)
{
    struct fake_function *host;
    struct fake_function *rtl;
    static const unsigned char address[6] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u
    };

    memset(pci, 0, sizeof(*pci));
    host = &pci->fp_function[0];
    host->ff_bus = 0;
    host->ff_device = 0;
    host->ff_config[0] = 0x12378086u;
    host->ff_config[PCI_CONFIG_CLASS_REVISION / 4u] = 0x06000002u;

    rtl = &pci->fp_function[1];
    rtl->ff_bus = 0;
    rtl->ff_device = 16;
    rtl->ff_config[0] = 0x816910ecu;
    rtl->ff_config[PCI_CONFIG_COMMAND_STATUS / 4u] = 0x00100000u;
    rtl->ff_config[PCI_CONFIG_CLASS_REVISION / 4u] = 0x02000010u;
    rtl->ff_config[PCI_CONFIG_BAR(0) / 4u] = 0x0000c001u;
    rtl->ff_config[PCI_CONFIG_BAR(1) / 4u] = 0xf0000000u;
    rtl->ff_config[PCI_CONFIG_BAR(2) / 4u] = 0xe0000004u;
    rtl->ff_config[PCI_CONFIG_BAR(3) / 4u] = 0x00000001u;
    rtl->ff_config[PCI_CONFIG_INTERRUPT / 4u] = 11u;
    rtl->ff_bar_mask[0] = 0xffffff01u;
    rtl->ff_bar_mask[1] = 0xffffff00u;
    rtl->ff_bar_mask[2] = 0xfffff004u;
    rtl->ff_bar_mask[3] = 0xffffffffu;

    memcpy(&pci->fp_registers[RTL_REG_MAC0], address, sizeof(address));
    fake_store32(pci->fp_registers, RTL_REG_TX_CONFIG, 0x040u << 20);
    pci->fp_registers[RTL_REG_PHY_STATUS] = RTL_PHY_STATUS_LINK;
    pci->fp_phy[4] = 0x0001u;
}

static void
receive_callback(void *arg, const unsigned char *frame, unsigned length)
{
    struct callback_state *state;

    state = arg;
    CHECK(length <= sizeof(state->cs_frame));
    memcpy(state->cs_frame, frame, length);
    state->cs_length = length;
    state->cs_rx_count++;
}

static void
receive_error_callback(void *arg)
{
    struct callback_state *state;

    state = arg;
    state->cs_rx_errors++;
}

static void
transmit_callback(void *arg, unsigned completed, unsigned errors)
{
    struct callback_state *state;

    state = arg;
    state->cs_tx_completed += completed;
    state->cs_tx_errors += errors;
}

static void
link_callback(void *arg, int link)
{
    struct callback_state *state;

    state = arg;
    state->cs_link_changes++;
    state->cs_link = link;
}

static const struct rtl8169_callbacks callbacks = {
    receive_callback,
    receive_error_callback,
    transmit_callback,
    link_callback,
};

static void
test_pci_and_rtl8169(void)
{
    struct fake_pci pci;
    struct pci_bus bus;
    struct pci_device device;
    struct pci_resource resource;
    struct rtl8169_softc unsupported;
    struct rtl8169_softc rtl;
    struct callback_state state;
    struct kinfo_pci_inventory *inventory;
    unsigned char frame[64];
    unsigned saved_command_status;
    int error;

    fake_init(&pci);
    memset(&bus, 0, sizeof(bus));
    memset(&state, 0, sizeof(state));
    CHECK(pci_bus_scan(&bus, &fake_ops, &pci) == 0);
    CHECK(pci_bus_function_count(&bus) == 2u);
    CHECK(inventory_provider != 0);
    inventory = inventory_provider();
    CHECK(inventory != 0 && inventory->kpi_count == 2u);
    CHECK(pci_find_device(&bus, RTL8169_VENDOR_REALTEK,
        RTL8169_PRODUCT_8169, &device));
    CHECK(device.pd_bus_number == 0 && device.pd_device == 16);
    CHECK(device.pd_revision == 0x10u);

    saved_command_status = pci.fp_function[1].ff_config[
        PCI_CONFIG_COMMAND_STATUS / 4u];
    pci_config_write16(&device, PCI_CONFIG_COMMAND_STATUS,
        PCI_COMMAND_MASTER);
    CHECK(pci.fp_function[1].ff_config[
        PCI_CONFIG_COMMAND_STATUS / 4u] ==
        ((saved_command_status & 0xffff0000u) | PCI_COMMAND_MASTER));
    pci_config_write16(&device, PCI_CONFIG_COMMAND_STATUS, 0);

    CHECK(pci_map_bar(&device, 0, 256u, &resource) == 0);
    CHECK(resource.pr_type == PCI_RESOURCE_IO);
    CHECK(resource.pr_address == 0xc000ull && resource.pr_size == 256u);
    CHECK(pci_map_bar(&device, 2, 4096u, &resource) == 0);
    CHECK(resource.pr_type == PCI_RESOURCE_MEMORY);
    CHECK(resource.pr_address == 0x1e0000000ull &&
        resource.pr_size == 4096u);

    fake_store32(pci.fp_registers, RTL_REG_TX_CONFIG, 0x200u << 20);
    CHECK(rtl8169_attach(&unsupported, &device, &callbacks, &state) ==
        EOPNOTSUPP);
    CHECK(pci_config_read16(&device, PCI_CONFIG_COMMAND_STATUS) == 0);
    CHECK(pci_config_read8(&device, 0x0cu) == 0);
    CHECK(pci_config_read8(&device, 0x0du) == 0);

    fake_store32(pci.fp_registers, RTL_REG_TX_CONFIG, 0x040u << 20);
    CHECK(dma_pool_init(fake_dma, FAKE_DMA_ADDRESS, sizeof(fake_dma),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0) == 0);
    error = rtl8169_attach(&rtl, &device, &callbacks, &state);
    if (error != 0)
        fprintf(stderr, "rtl8169 attach error: %d\n", error);
    CHECK(error == 0);
    CHECK(rtl.rs_mac_version == 3u && rtl.rs_xid == 0x040u);
    CHECK(rtl.rs_registers.pr_type == PCI_RESOURCE_MEMORY);
    CHECK(rtl.rs_registers.pr_bar == 1u);
    CHECK(pci.fp_irq == 11u && pci.fp_handler == rtl8169_interrupt);
    CHECK(rtl8169_start(&rtl) == 0);
    CHECK(rtl8169_link_up(&rtl));
    CHECK(state.cs_link_changes == 1u && state.cs_link == 1);
    CHECK(rtl8169_tx_available(&rtl) == RTL8169_TX_DESCRIPTORS);
    CHECK(fake_load16(pci.fp_registers, RTL_REG_RX_MAX_SIZE) == 16384u);
    CHECK(fake_load16(pci.fp_registers, RTL_REG_INTERRUPT_MASK) != 0);

    memset(frame, 0x5a, sizeof(frame));
    CHECK(rtl8169_transmit(&rtl, frame, sizeof(frame)) == 0);
    CHECK(rtl.rs_tx_used == 1u);
    CHECK((rtl.rs_tx_desc[0].rd_opts1 & RTL_DESCRIPTOR_OWN) != 0);
    CHECK(pci.fp_registers[RTL_REG_TX_POLL] == RTL_TX_POLL_NORMAL);
    rtl.rs_tx_desc[0].rd_opts1 &= ~RTL_DESCRIPTOR_OWN;
    fake_store16(pci.fp_registers, RTL_REG_INTERRUPT_STATUS,
        RTL_INTERRUPT_TX_OK);
    CHECK(pci.fp_handler(pci.fp_handler_arg) == 1);
    CHECK(state.cs_tx_completed == 1u && state.cs_tx_errors == 0);
    CHECK(rtl.rs_tx_used == 0u);

    memcpy(rtl.rs_rx_buffers, frame, sizeof(frame));
    rtl.rs_rx_desc[0].rd_opts1 = RTL_DESCRIPTOR_FIRST |
        RTL_DESCRIPTOR_LAST | (sizeof(frame) + 4u);
    fake_store16(pci.fp_registers, RTL_REG_INTERRUPT_STATUS,
        RTL_INTERRUPT_RX_OK);
    CHECK(pci.fp_handler(pci.fp_handler_arg) == 1);
    CHECK(state.cs_rx_count == 1u && state.cs_length == sizeof(frame));
    CHECK(memcmp(state.cs_frame, frame, sizeof(frame)) == 0);
    CHECK((rtl.rs_rx_desc[0].rd_opts1 & RTL_DESCRIPTOR_OWN) != 0);

    rtl.rs_rx_desc[1].rd_opts1 = RTL_DESCRIPTOR_FIRST | 68u;
    fake_store16(pci.fp_registers, RTL_REG_INTERRUPT_STATUS,
        RTL_INTERRUPT_RX_ERROR);
    CHECK(pci.fp_handler(pci.fp_handler_arg) == 1);
    CHECK(state.cs_rx_errors == 1u);

    pci.fp_registers[RTL_REG_PHY_STATUS] = 0;
    fake_store16(pci.fp_registers, RTL_REG_INTERRUPT_STATUS,
        RTL_INTERRUPT_LINK_CHANGE);
    CHECK(pci.fp_handler(pci.fp_handler_arg) == 1);
    CHECK(state.cs_link_changes == 2u && state.cs_link == 0);

    CHECK(rtl8169_transmit(&rtl, frame, sizeof(frame)) == 0);
    fake_store16(pci.fp_registers, RTL_REG_INTERRUPT_STATUS,
        RTL_INTERRUPT_SYSTEM_ERROR);
    CHECK(pci.fp_handler(pci.fp_handler_arg) == 1);
    CHECK(state.cs_tx_completed == 2u && state.cs_tx_errors == 1u);
    CHECK(rtl.rs_running && rtl.rs_tx_used == 0u);

    rtl8169_stop(&rtl);
    CHECK(dma_free(&rtl.rs_dma) == 0);
}

int
main(void)
{
    test_pci_and_rtl8169();
    puts("pci/rtl8169 tests: ok");
    return 0;
}
