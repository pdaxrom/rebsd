/*
 * Fake-I/O tests for UHCI control, bulk chunking, low-speed TDs and ports.
 */

#include <stdio.h>
#include <string.h>
#include <usb/uhcivar.h>
#include <usb/ukbd.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define FAKE_POOL_SIZE     (64u * 1024u)
#define FAKE_POOL_PHYS     0x01800000u
#define FAKE_BULK_BYTES    (40u * 1024u)

static unsigned char dma_pool[FAKE_POOL_SIZE]
    __attribute__((aligned(UHCI_FRAME_LIST_ALIGN)));
static unsigned char bulk_out[FAKE_BULK_BYTES];
static unsigned char bulk_in[FAKE_BULK_BYTES];

static const unsigned char fake_device_desc[] = {
    18, UDESC_DEVICE, 0x10, 0x01, 0, 0, 0, 8,
    0xcd, 0xab, 0x34, 0x12, 0x00, 0x01, 0, 0, 0, 1
};

static const unsigned char fake_bulk_config_desc[] = {
    9, UDESC_CONFIG, 32, 0, 1, 1, 0, UC_BUS_POWERED, 25,
    9, UDESC_INTERFACE, 0, 0, 2, 0xff, 0, 0, 0,
    7, UDESC_ENDPOINT, 0x01, UE_BULK, 64, 0, 0,
    7, UDESC_ENDPOINT, 0x81, UE_BULK, 64, 0, 0
};

static const unsigned char fake_keyboard_config_desc[] = {
    9, UDESC_CONFIG, 25, 0, 1, 1, 0, UC_BUS_POWERED, 25,
    9, UDESC_INTERFACE, 0, 0, 1,
        UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
    7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
};

struct fake_uhci {
    unsigned short command;
    unsigned short status;
    unsigned short intr;
    unsigned short frame;
    unsigned short ports[2];
    unsigned int frame_base;
    usb_device_request_t request;
    unsigned request_valid;
    unsigned control_offset;
    unsigned address;
    unsigned configuration;
    unsigned delay_total;
    unsigned lists;
    unsigned chunks;
    unsigned saw_low_speed;
    size_t bulk_out_length;
    size_t bulk_in_length;
    unsigned bulk_device;
};

void
cninput(int character)
{
    (void)character;
}

static void *
phys_to_ptr(unsigned int phys)
{
    if (phys < FAKE_POOL_PHYS || phys >= FAKE_POOL_PHYS + FAKE_POOL_SIZE)
        return 0;
    return dma_pool + (phys - FAKE_POOL_PHYS);
}

static size_t
minimum(size_t a, size_t b)
{
    return a < b ? a : b;
}

/*
 * Host tests run on little-endian builders in the supported CI matrix.
 * Keep descriptor access explicit so the fake controller still documents
 * that it is reading little-endian schedule words.
 */
static unsigned
schedule_word(unsigned value)
{
    const unsigned short one = 1;

    if (*(const unsigned char *)&one != 1)
        return ((value & 0x000000ffu) << 24) |
            ((value & 0x0000ff00u) << 8) |
            ((value & 0x00ff0000u) >> 8) |
            ((value & 0xff000000u) >> 24);
    return value;
}

#undef uhci_from_le32
#define uhci_from_le32(v) schedule_word(v)

static void
complete_td(struct fake_uhci *fake, struct uhci_td *td, unsigned actlen)
{
    unsigned status;

    status = schedule_word(td->td_status);
    if (status & UHCI_TD_LS)
        fake->saw_low_speed = 1;
    status &= ~(UHCI_TD_ACTIVE | UHCI_TD_ACTLEN_MASK);
    status |= actlen == 0 ? UHCI_TD_ZERO_ACTLEN :
        (actlen - 1u) & UHCI_TD_ACTLEN_MASK;
    td->td_status = schedule_word(status);
    if (status & UHCI_TD_IOC)
        fake->status |= UHCI_STS_USBINT;
}

static const unsigned char *
control_source(struct fake_uhci *fake, size_t *length)
{
    unsigned type;

    if (fake->request.bRequest != UR_GET_DESCRIPTOR)
        return 0;
    type = UGETW(fake->request.wValue) >> 8;
    if (type == UDESC_DEVICE) {
        *length = sizeof(fake_device_desc);
        return fake_device_desc;
    }
    if (type == UDESC_CONFIG) {
        if (fake->bulk_device) {
            *length = sizeof(fake_bulk_config_desc);
            return fake_bulk_config_desc;
        }
        *length = sizeof(fake_keyboard_config_desc);
        return fake_keyboard_config_desc;
    }
    return 0;
}

static int
fake_run_td_chain(struct fake_uhci *fake, struct uhci_qh *qh)
{
    struct uhci_td *td;
    const unsigned char *source;
    unsigned char *buffer;
    unsigned link;
    unsigned token;
    unsigned pid;
    unsigned length;
    unsigned copied;
    size_t source_length;
    size_t remaining;
    unsigned steps;

    link = schedule_word(qh->qh_elink);
    for (steps = 0; steps < UHCI_TD_COUNT && !(link & UHCI_PTR_T);
        ++steps) {
        td = phys_to_ptr(link & UHCI_PTR_MASK);
        if (td == 0)
            return -1;
        if ((schedule_word(td->td_status) & UHCI_TD_ACTIVE) == 0) {
            link = schedule_word(td->td_link);
            qh->qh_elink = schedule_word(link);
            continue;
        }
        token = schedule_word(td->td_token);
        pid = UHCI_TD_GET_PID(token);
        length = UHCI_TD_GET_MAXLEN(token);
        buffer = length == 0 ? 0 :
            phys_to_ptr(schedule_word(td->td_buffer));
        if (length != 0 && buffer == 0)
            return -1;

        if (pid == UHCI_TD_PID_SETUP) {
            memcpy(&fake->request, buffer, sizeof(fake->request));
            fake->request_valid = 1;
            fake->control_offset = 0;
            complete_td(fake, td, sizeof(fake->request));
        } else if (fake->request_valid && length == 0) {
            if (fake->request.bRequest == UR_SET_ADDRESS)
                fake->address = UGETW(fake->request.wValue);
            if (fake->request.bRequest == UR_SET_CONFIG)
                fake->configuration = UGETW(fake->request.wValue);
            fake->request_valid = 0;
            complete_td(fake, td, 0);
        } else if (fake->request_valid && pid == UHCI_TD_PID_IN) {
            source_length = 0;
            source = control_source(fake, &source_length);
            if (source == 0)
                return -1;
            remaining = fake->control_offset < source_length ?
                source_length - fake->control_offset : 0;
            copied = (unsigned)minimum(length, remaining);
            if (copied != 0)
                memcpy(buffer, source + fake->control_offset, copied);
            fake->control_offset += copied;
            complete_td(fake, td, copied);
            link = schedule_word(td->td_link);
            qh->qh_elink = schedule_word(link);
            if (copied < length)
                break;
            continue;
        } else if (fake->request_valid) {
            complete_td(fake, td, length);
        } else if (pid == UHCI_TD_PID_OUT) {
            if (fake->bulk_out_length + length > sizeof(bulk_out))
                return -1;
            memcpy(bulk_out + fake->bulk_out_length, buffer, length);
            fake->bulk_out_length += length;
            complete_td(fake, td, length);
        } else if (pid == UHCI_TD_PID_IN) {
            if (fake->bulk_in_length + length > sizeof(bulk_in))
                return -1;
            memcpy(buffer, bulk_in + fake->bulk_in_length, length);
            fake->bulk_in_length += length;
            complete_td(fake, td, length);
        } else {
            return -1;
        }
        link = schedule_word(td->td_link);
        qh->qh_elink = schedule_word(link);
    }
    return 0;
}

static void
fake_run_schedule(struct fake_uhci *fake)
{
    unsigned int *frames;
    struct uhci_qh *qh;
    unsigned link;
    unsigned steps;
    unsigned active;

    if ((fake->command & UHCI_CMD_RS) == 0 || fake->frame_base == 0)
        return;
    frames = phys_to_ptr(fake->frame_base & UHCI_PTR_MASK);
    if (frames == 0)
        return;
    link = schedule_word(frames[fake->frame & UHCI_FRNUM_MASK]);
    active = 0;
    for (steps = 0; steps < 8u && !(link & UHCI_PTR_T); ++steps) {
        if ((link & UHCI_PTR_QH) == 0)
            return;
        qh = phys_to_ptr(link & UHCI_PTR_MASK);
        if (qh == 0)
            return;
        if (!(schedule_word(qh->qh_elink) & UHCI_PTR_T)) {
            active = 1;
            if (fake_run_td_chain(fake, qh) != 0)
                return;
        }
        link = schedule_word(qh->qh_hlink);
    }
    if (active) {
        ++fake->lists;
        ++fake->chunks;
    }
    fake->frame = (fake->frame + 1u) & UHCI_FRNUM_MASK;
}

static unsigned short
fake_read_2(void *arg, unsigned reg)
{
    struct fake_uhci *fake;

    fake = arg;
    switch (reg) {
    case UHCI_CMD:
        return fake->command;
    case UHCI_STS:
        return fake->status;
    case UHCI_INTR:
        return fake->intr;
    case UHCI_FRNUM:
        return fake->frame;
    case UHCI_PORTSC1:
        return fake->ports[0];
    case UHCI_PORTSC2:
        return fake->ports[1];
    default:
        return 0;
    }
}

static void
fake_write_port(struct fake_uhci *fake, unsigned port,
    unsigned short value)
{
    unsigned short status;

    status = fake->ports[port];
    status &= (unsigned short)~(value & UHCI_PORTSC_W1C);
    status &= (unsigned short)~UHCI_PORTSC_RW;
    status |= value & UHCI_PORTSC_RW;
    fake->ports[port] = status;
}

static void
fake_write_2(void *arg, unsigned reg, unsigned short value)
{
    struct fake_uhci *fake;

    fake = arg;
    switch (reg) {
    case UHCI_CMD:
        if (value & UHCI_CMD_HCRESET) {
            fake->command = 0;
            fake->status = UHCI_STS_HCH;
        } else {
            fake->command = value;
            if (value & UHCI_CMD_RS)
                fake->status &= (unsigned short)~UHCI_STS_HCH;
            else
                fake->status |= UHCI_STS_HCH;
        }
        break;
    case UHCI_STS:
        fake->status &= (unsigned short)~(value & UHCI_STS_ACK);
        break;
    case UHCI_INTR:
        fake->intr = value;
        break;
    case UHCI_FRNUM:
        fake->frame = value & UHCI_FRNUM_MASK;
        break;
    case UHCI_PORTSC1:
        fake_write_port(fake, 0, value);
        break;
    case UHCI_PORTSC2:
        fake_write_port(fake, 1, value);
        break;
    default:
        break;
    }
}

static unsigned int
fake_read_4(void *arg, unsigned reg)
{
    struct fake_uhci *fake;

    fake = arg;
    return reg == UHCI_FLBASEADDR ? fake->frame_base : 0;
}

static void
fake_write_4(void *arg, unsigned reg, unsigned int value)
{
    struct fake_uhci *fake;

    fake = arg;
    if (reg == UHCI_FLBASEADDR)
        fake->frame_base = value;
}

static void
fake_delay(void *arg, unsigned milliseconds)
{
    struct fake_uhci *fake;

    fake = arg;
    fake->delay_total += milliseconds;
    fake_run_schedule(fake);
}

static int
start_fake(struct fake_uhci *fake, struct uhci_softc *uhci,
    struct usb_core *core, struct usb_bus *bus, unsigned bulk_device)
{
    memset(fake, 0, sizeof(*fake));
    memset(bus, 0, sizeof(*bus));
    usb_core_init(core);
    fake->bulk_device = bulk_device;
    fake->status = UHCI_STS_HCH;
    fake->ports[0] = UHCI_PORTSC_CCS | UHCI_PORTSC_CSC;
    uhci_softc_init(uhci, fake_read_2, fake_write_2,
        fake_read_4, fake_write_4, fake_delay, fake);
    return usb_bus_start(core, bus, &uhci->uh_hcd, fake_delay, fake);
}

static int
test_uhci_control_and_ports(void)
{
    struct fake_uhci fake;
    struct uhci_softc uhci;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *device;
    usb_port_status_t status;

    CHECK(start_fake(&fake, &uhci, &core, &bus, 0) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(uhci.uh_started);
    CHECK((fake.frame_base & (UHCI_FRAME_LIST_ALIGN - 1u)) == 0);
    CHECK((fake.command & (UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP)) ==
        (UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP));
    CHECK(uhci_root_port_status(&uhci, 1, &status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK((UGETW(status.wPortStatus) &
        (UPS_CURRENT_CONNECT_STATUS | UPS_PORT_POWER)) ==
        (UPS_CURRENT_CONNECT_STATUS | UPS_PORT_POWER));
    CHECK(uhci_root_port_reset(&uhci, 1) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(uhci_root_port_status(&uhci, 1, &status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK((UGETW(status.wPortStatus) & UPS_PORT_ENABLED) != 0);
    CHECK((UGETW(status.wPortChange) & UPS_C_PORT_RESET) != 0);
    CHECK(uhci_root_port_clear_change(&uhci, 1,
        UGETW(status.wPortChange)) == USB_STATUS_NORMAL_COMPLETION);

    CHECK(ukbd_register(&core) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_LOW, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(device != 0 && device->ud_address == 1);
    CHECK(UGETW(device->ud_desc.idVendor) == 0xabcd);
    CHECK(fake.address == 1 && fake.configuration == 1);
    CHECK(fake.saw_low_speed);
    CHECK(uhci.uh_intr_xfer != 0);
    CHECK((schedule_word(uhci.uh_intr_td->td_status) & UHCI_TD_LS) != 0);
    usb_device_disconnect(device);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

static int
test_uhci_bulk_chunks(void)
{
    struct fake_uhci fake;
    struct uhci_softc uhci;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *device;
    struct usb_interface *interface;
    struct usb_pipe *out_pipe;
    struct usb_pipe *in_pipe;
    unsigned char out_data[FAKE_BULK_BYTES];
    unsigned char in_data[FAKE_BULK_BYTES];
    size_t actlen;
    unsigned control_lists;
    unsigned i;

    for (i = 0; i < sizeof(out_data); ++i) {
        out_data[i] = (unsigned char)(i ^ (i >> 8));
        bulk_in[i] = (unsigned char)(0xa5u ^ i);
    }
    memset(in_data, 0, sizeof(in_data));
    memset(bulk_out, 0, sizeof(bulk_out));
    CHECK(start_fake(&fake, &uhci, &core, &bus, 1) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_device_enumerate(&bus, 1, USB_SPEED_FULL, &device) ==
        USB_STATUS_NORMAL_COMPLETION);
    interface = usb_device_interface(device, 0);
    CHECK(interface != 0);
    CHECK(usb_open_pipe(interface, 0x01, &out_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_open_pipe(interface, 0x81, &in_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    control_lists = fake.lists;

    CHECK(usb_bulk_transfer(out_pipe, out_data, sizeof(out_data), 0,
        USB_DEFAULT_TIMEOUT_MS, &actlen) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(actlen == sizeof(out_data));
    CHECK(fake.bulk_out_length == sizeof(out_data));
    CHECK(memcmp(bulk_out, out_data, sizeof(out_data)) == 0);
    CHECK(fake.lists > control_lists + 2u);

    fake.chunks = 0;
    CHECK(usb_bulk_transfer(in_pipe, in_data, sizeof(in_data), 0,
        USB_DEFAULT_TIMEOUT_MS, &actlen) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(actlen == sizeof(in_data));
    CHECK(fake.bulk_in_length == sizeof(in_data));
    CHECK(memcmp(in_data, bulk_in, sizeof(in_data)) == 0);
    CHECK(fake.chunks > 2u);

    usb_close_pipe(in_pipe);
    usb_close_pipe(out_pipe);
    usb_device_disconnect(device);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

int
main(void)
{
    CHECK(dma_pool_init(dma_pool, FAKE_POOL_PHYS, sizeof(dma_pool),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0) == 0);
    CHECK(test_uhci_control_and_ports() == 0);
    CHECK(test_uhci_bulk_chunks() == 0);
    puts("uhci tests: ok");
    return 0;
}
