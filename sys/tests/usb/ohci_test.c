/*
 * Fake-MMIO tests for the polling OHCI control schedule.
 */

#include <stdio.h>
#include <string.h>
#include <dev/usb/ohcivar.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define FAKE_REG_WORDS 64
#define FAKE_POOL_SIZE  (16u * 1024u)
#define FAKE_POOL_PHYS  0x01000000u

static unsigned char dma_pool[FAKE_POOL_SIZE]
    __attribute__((aligned(4096)));

static const unsigned char fake_device_desc[] = {
    18, UDESC_DEVICE, 0x10, 0x01, 0, 0, 0, 8,
    0xcd, 0xab, 0x34, 0x12, 0x00, 0x01, 0, 0, 0, 1
};

static const unsigned char fake_config_desc[] = {
    9, UDESC_CONFIG, 25, 0, 1, 1, 0, UC_BUS_POWERED, 25,
    9, UDESC_INTERFACE, 0, 0, 1,
        UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
    7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
};

struct fake_ohci {
    unsigned int regs[FAKE_REG_WORDS];
    unsigned delay_total;
    unsigned control_lists;
    unsigned address;
    unsigned configuration;
    unsigned next_cc;
    unsigned hold;
    unsigned reset_reads;
};

static void *
phys_to_ptr(unsigned int phys)
{
    if (phys < FAKE_POOL_PHYS || phys >= FAKE_POOL_PHYS + FAKE_POOL_SIZE)
        return 0;
    return dma_pool + (phys - FAKE_POOL_PHYS);
}

static void
set_cc(struct ohci_td *td, unsigned cc)
{
    td->td_flags = (td->td_flags & 0x0fffffffu) | (cc << 28);
}

static size_t
minimum(size_t a, size_t b)
{
    return a < b ? a : b;
}

static void
fake_run_control(struct fake_ohci *fake)
{
    struct ohci_ed *ed;
    struct ohci_td *setup_td;
    struct ohci_td *data_td;
    struct ohci_td *status_td;
    usb_device_request_t *request;
    const unsigned char *source;
    unsigned char *data;
    unsigned int head;
    unsigned int next;
    unsigned int value;
    unsigned int type;
    size_t requested;
    size_t copied;

    if (fake->hold)
        return;
    ed = phys_to_ptr(fake->regs[OHCI_CONTROL_HEAD_ED / 4]);
    if (ed == 0)
        return;
    head = ed->ed_headp & OHCI_ED_HEADMASK;
    setup_td = phys_to_ptr(head);
    if (setup_td == 0)
        return;
    request = phys_to_ptr(setup_td->td_cbp);
    if (request == 0)
        return;
    ++fake->control_lists;

    next = setup_td->td_nexttd;
    data_td = 0;
    if (UGETW(request->wLength) != 0) {
        data_td = phys_to_ptr(next);
        if (data_td == 0)
            return;
        next = data_td->td_nexttd;
    }
    status_td = phys_to_ptr(next);
    if (status_td == 0)
        return;

    if (fake->next_cc != OHCI_CC_NO_ERROR) {
        set_cc(setup_td, fake->next_cc);
        fake->next_cc = OHCI_CC_NO_ERROR;
        ed->ed_headp = ed->ed_tailp;
        return;
    }

    source = 0;
    requested = UGETW(request->wLength);
    value = UGETW(request->wValue);
    type = value >> 8;
    if (request->bRequest == UR_GET_DESCRIPTOR) {
        if (type == UDESC_DEVICE)
            source = fake_device_desc;
        else if (type == UDESC_CONFIG)
            source = fake_config_desc;
        if (source == 0) {
            set_cc(setup_td, OHCI_CC_STALL);
            ed->ed_headp = ed->ed_tailp;
            return;
        }
        copied = minimum(requested, type == UDESC_DEVICE ?
            sizeof(fake_device_desc) : sizeof(fake_config_desc));
        data = phys_to_ptr(data_td->td_cbp);
        memcpy(data, source, copied);
        if (copied == requested) {
            data_td->td_cbp = 0;
            set_cc(data_td, OHCI_CC_NO_ERROR);
        } else {
            data_td->td_cbp += (unsigned int)copied;
            set_cc(data_td, OHCI_CC_DATA_UNDERRUN);
        }
    } else if (request->bRequest == UR_SET_ADDRESS) {
        fake->address = value;
    } else if (request->bRequest == UR_SET_CONFIG) {
        fake->configuration = value;
    } else {
        set_cc(setup_td, OHCI_CC_STALL);
        ed->ed_headp = ed->ed_tailp;
        return;
    }
    set_cc(setup_td, OHCI_CC_NO_ERROR);
    set_cc(status_td, OHCI_CC_NO_ERROR);
    ed->ed_headp = ed->ed_tailp;
}

static unsigned int
fake_read(void *arg, unsigned reg)
{
    struct fake_ohci *fake;
    unsigned int value;

    fake = arg;
    if (reg == OHCI_RH_PORT_STATUS(1) && fake->reset_reads != 0) {
        --fake->reset_reads;
        if (fake->reset_reads == 0) {
            fake->regs[reg / 4] &= ~OHCI_RHPS_PRS;
            fake->regs[reg / 4] |= OHCI_RHPS_PRSC | OHCI_RHPS_PES;
        }
    }
    value = fake->regs[reg / 4];
    return value;
}

static void
fake_write(void *arg, unsigned reg, unsigned int value)
{
    struct fake_ohci *fake;
    unsigned int *slot;

    fake = arg;
    slot = &fake->regs[reg / 4];
    if (reg == OHCI_COMMAND_STATUS) {
        if (value & OHCI_HCR) {
            *slot = 0;
            return;
        }
        if (value & OHCI_CLF) {
            fake_run_control(fake);
            *slot = 0;
            return;
        }
    }
    if (reg == OHCI_INTERRUPT_STATUS) {
        *slot &= ~value;
        return;
    }
    if (reg == OHCI_RH_PORT_STATUS(1)) {
        if (value & OHCI_RHPS_SPP)
            *slot |= OHCI_RHPS_PPS;
        if (value & OHCI_RHPS_CPP)
            *slot &= ~OHCI_RHPS_PPS;
        if (value & OHCI_RHPS_SPRS) {
            *slot |= OHCI_RHPS_PRS;
            fake->reset_reads = 2;
        }
        if (value & OHCI_RHPS_PRSC)
            *slot &= ~OHCI_RHPS_PRSC;
        return;
    }
    *slot = value;
}

static void
fake_delay(void *arg, unsigned milliseconds)
{
    struct fake_ohci *fake;

    fake = arg;
    fake->delay_total += milliseconds;
}

static int
test_ohci_enumeration(void)
{
    struct fake_ohci fake;
    struct ohci_softc ohci;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *device;
    usb_port_status_t port_status;

    memset(&fake, 0, sizeof(fake));
    memset(&bus, 0, sizeof(bus));
    usb_core_init(&core);
    fake.regs[OHCI_REVISION / 4] = 0x10;
    fake.regs[OHCI_FM_INTERVAL / 4] = OHCI_DEFAULT_FI;
    fake.regs[OHCI_RH_DESCRIPTOR_A / 4] = 1 | (1u << 24);
    fake.regs[OHCI_RH_PORT_STATUS(1) / 4] =
        OHCI_RHPS_CCS | OHCI_RHPS_PPS;
    ohci_softc_init(&ohci, fake_read, fake_write, fake_delay, &fake);
    CHECK(usb_bus_start(&core, &bus, &ohci.oh_hcd, fake_delay, &fake) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(ohci.oh_started && ohci.oh_nports == 1);
    CHECK((fake.regs[OHCI_HCCA / 4] & (OHCI_HCCA_ALIGN - 1)) == 0);
    CHECK(fake.regs[OHCI_CONTROL / 4] ==
        (OHCI_RATIO_1_4 | OHCI_HCFS_OPERATIONAL));

    CHECK(ohci_root_port_status(&ohci, 1, &port_status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK((UGETW(port_status.wPortStatus) &
        (UPS_CURRENT_CONNECT_STATUS | UPS_PORT_POWER)) ==
        (UPS_CURRENT_CONNECT_STATUS | UPS_PORT_POWER));
    CHECK(ohci_root_port_reset(&ohci, 1) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(ohci_root_port_status(&ohci, 1, &port_status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(UGETW(port_status.wPortStatus) & UPS_PORT_ENABLED);

    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(device != 0 && device->ud_address == 1);
    CHECK(UGETW(device->ud_desc.idVendor) == 0xabcd);
    CHECK(UGETW(device->ud_desc.idProduct) == 0x1234);
    CHECK(fake.address == 1 && fake.configuration == 1);
    CHECK(fake.control_lists == 6);
    usb_device_disconnect(device);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

static int
test_ohci_errors(void)
{
    struct fake_ohci fake;
    struct ohci_softc ohci;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *device;

    memset(&fake, 0, sizeof(fake));
    memset(&bus, 0, sizeof(bus));
    usb_core_init(&core);
    fake.regs[OHCI_REVISION / 4] = 0x10;
    fake.regs[OHCI_FM_INTERVAL / 4] = OHCI_DEFAULT_FI;
    fake.regs[OHCI_RH_DESCRIPTOR_A / 4] = 1;
    fake.regs[OHCI_RH_PORT_STATUS(1) / 4] = OHCI_RHPS_CCS;
    ohci_softc_init(&ohci, fake_read, fake_write, fake_delay, &fake);
    CHECK(usb_bus_start(&core, &bus, &ohci.oh_hcd, fake_delay, &fake) ==
        USB_STATUS_NORMAL_COMPLETION);
    fake.next_cc = OHCI_CC_STALL;
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_STALLED);
    CHECK(device == 0 && bus.ub_addresses[0] == 0);
    fake.hold = 1;
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_TIMEOUT);
    CHECK(device == 0 && ohci.oh_active_xfer == 0);
    fake.hold = 0;
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    usb_device_disconnect(device);
    usb_bus_stop(&bus);
    return 0;
}

int
main(void)
{
    CHECK(sizeof(struct ohci_hcca) == OHCI_HCCA_SIZE);
    CHECK(sizeof(struct ohci_ed) == OHCI_ED_ALIGN);
    CHECK(sizeof(struct ohci_td) == OHCI_TD_ALIGN);
    CHECK(dma_pool_init(dma_pool, FAKE_POOL_PHYS, sizeof(dma_pool),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0) == 0);
    CHECK(test_ohci_enumeration() == 0);
    CHECK(test_ohci_errors() == 0);
    puts("ohci_test: all tests passed");
    return 0;
}
