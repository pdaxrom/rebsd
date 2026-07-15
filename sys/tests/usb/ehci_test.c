/*
 * Fake-MMIO tests for compact EHCI async transfers and companion handoff.
 */

#include <stdio.h>
#include <string.h>
#include <usb/ehcivar.h>
#include <usb/uhub.h>
#include <usb/usb_task.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

#define FAKE_REG_WORDS             128u
#define FAKE_POOL_SIZE             (64u * 1024u)
#define FAKE_POOL_PHYS             0x02000000u
#define FAKE_CAP_LENGTH            0x20u
#define FAKE_OP(reg)               ((FAKE_CAP_LENGTH + (reg)) / 4u)
#define FAKE_PORT                  FAKE_OP(EHCI_PORTSC(1))
#define FAKE_TOKEN_BYTES_MASK      0x7fff0000u

static unsigned char dma_pool[FAKE_POOL_SIZE]
    __attribute__((aligned(4096)));

static const unsigned char fake_device_desc[] = {
    18, UDESC_DEVICE, 0x00, 0x02, 0, 0, 0, 64,
    0xcd, 0xab, 0x34, 0x12, 0x00, 0x01, 0, 0, 0, 1
};

static const unsigned char fake_config_desc[] = {
    9, UDESC_CONFIG, 39, 0, 1, 1, 0, UC_BUS_POWERED, 25,
    9, UDESC_INTERFACE, 0, 0, 3, 8, 6, 0x50, 0,
    7, UDESC_ENDPOINT, 0x01, UE_BULK, 0x00, 0x02, 0,
    7, UDESC_ENDPOINT, 0x81, UE_BULK, 0x00, 0x02, 0,
    7, UDESC_ENDPOINT, 0x82, UE_INTERRUPT, 8, 0, 10
};

struct fake_ehci {
    unsigned int regs[FAKE_REG_WORDS];
    unsigned delay_total;
    unsigned port_speed;
    unsigned reset_active;
    unsigned control_lists;
    unsigned first_control_mpl;
    unsigned bad_current_qtd;
    unsigned reset_released_at;
    unsigned first_control_at;
    unsigned set_address_at;
    unsigned post_address_control_at;
    unsigned bulk_lists;
    unsigned address;
    unsigned configuration;
    unsigned hold;
    unsigned next_halt;
    unsigned bulk_toggle[8];
    unsigned bulk_toggle_count;
    unsigned periodic_lists;
    unsigned periodic_status;
    unsigned periodic_starts;
    unsigned periodic_stops;
    const unsigned char *bulk_reply;
    size_t bulk_reply_length;
    unsigned char bulk_out[64];
    size_t bulk_out_length;
};

static void *
phys_to_ptr(unsigned int phys)
{
    if (phys < FAKE_POOL_PHYS || phys >= FAKE_POOL_PHYS + FAKE_POOL_SIZE)
        return 0;
    return dma_pool + (phys - FAKE_POOL_PHYS);
}

static unsigned int
ptr_to_phys(const void *ptr)
{
    return FAKE_POOL_PHYS + (unsigned int)((const unsigned char *)ptr -
        dma_pool);
}

static size_t
minimum(size_t a, size_t b)
{
    return a < b ? a : b;
}

static unsigned
qtd_pid(const struct ehci_qtd *qtd)
{
    return (qtd->qtd_status >> 8) & 3u;
}

static unsigned
qtd_bytes(const struct ehci_qtd *qtd)
{
    return EHCI_QTD_GET_BYTES(qtd->qtd_status);
}

static void
qtd_complete(struct ehci_qtd *qtd, unsigned remaining, unsigned status)
{
    qtd->qtd_status &= ~(FAKE_TOKEN_BYTES_MASK | EHCI_QTD_ACTIVE |
        EHCI_QTD_HALTED | EHCI_QTD_STATERRS);
    if ((status & (EHCI_QTD_HALTED | EHCI_QTD_XACTERR)) ==
        (EHCI_QTD_HALTED | EHCI_QTD_XACTERR) &&
        (status & EHCI_QTD_MISSEDMICRO) == 0)
        qtd->qtd_status &= ~EHCI_QTD_CERR_MASK;
    qtd->qtd_status |= EHCI_QTD_SET_BYTES(remaining) | status;
}

static void
fake_run_control(struct fake_ehci *fake, struct ehci_qh *qh,
    struct ehci_qtd *setup)
{
    usb_device_request_t *request;
    struct ehci_qtd *data;
    struct ehci_qtd *status;
    const unsigned char *source;
    unsigned char *buffer;
    unsigned int next;
    unsigned type;
    unsigned value;
    size_t copied;
    size_t requested;

    request = phys_to_ptr(setup->qtd_buffer[0]);
    if (request == 0)
        return;
    if (fake->control_lists == 0) {
        fake->first_control_mpl = (qh->qh_endp >> 16) & 0x07ffu;
        fake->first_control_at = fake->delay_total;
    } else if (fake->address != 0 &&
        fake->post_address_control_at == 0)
        fake->post_address_control_at = fake->delay_total;
    if ((qh->qh_curqtd & 0x1fu) != 0)
        fake->bad_current_qtd = 1;
    ++fake->control_lists;
    requested = UGETW(request->wLength);
    next = setup->qtd_next;
    data = 0;
    if (requested != 0) {
        data = phys_to_ptr(EHCI_LINK_ADDR(next));
        if (data == 0)
            return;
        next = data->qtd_next;
    }
    status = phys_to_ptr(EHCI_LINK_ADDR(next));
    if (status == 0)
        return;

    source = 0;
    value = UGETW(request->wValue);
    type = value >> 8;
    if (request->bRequest == UR_GET_DESCRIPTOR) {
        if (type == UDESC_DEVICE)
            source = fake_device_desc;
        else if (type == UDESC_CONFIG)
            source = fake_config_desc;
        if (source == 0)
            goto stall;
        copied = minimum(requested, type == UDESC_DEVICE ?
            sizeof(fake_device_desc) : sizeof(fake_config_desc));
        buffer = phys_to_ptr(data->qtd_buffer[0]);
        if (buffer == 0)
            return;
        memcpy(buffer, source, copied);
        qtd_complete(data, (unsigned)(requested - copied), 0);
    } else if (request->bRequest == UR_SET_ADDRESS) {
        fake->address = value;
        fake->set_address_at = fake->delay_total;
    } else if (request->bRequest == UR_SET_CONFIG) {
        fake->configuration = value;
    } else {
        goto stall;
    }
    qtd_complete(setup, 0, 0);
    qtd_complete(status, 0, 0);
    qh->qh_qtd.qtd_next = EHCI_LINK_TERMINATE;
    fake->regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_INT;
    return;

stall:
    qtd_complete(setup, 0, EHCI_QTD_HALTED);
    fake->regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_ERRINT;
}

static void
fake_run_bulk(struct fake_ehci *fake, struct ehci_qh *qh,
    struct ehci_qtd *qtd)
{
    unsigned char *buffer;
    unsigned requested;
    unsigned pid;
    size_t copied;

    requested = qtd_bytes(qtd);
    pid = qtd_pid(qtd);
    if (fake->bulk_toggle_count <
        sizeof(fake->bulk_toggle) / sizeof(fake->bulk_toggle[0]))
        fake->bulk_toggle[fake->bulk_toggle_count++] =
            EHCI_QTD_GET_TOGGLE(qtd->qtd_status);
    ++fake->bulk_lists;
    if (fake->next_halt != 0) {
        qtd_complete(qtd, requested, fake->next_halt);
        fake->next_halt = 0;
        fake->regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_ERRINT;
        return;
    }
    buffer = requested == 0 ? 0 : phys_to_ptr(qtd->qtd_buffer[0]);
    if (requested != 0 && buffer == 0)
        return;
    if (pid == EHCI_QTD_PID_IN) {
        copied = minimum(requested, fake->bulk_reply_length);
        if (copied != 0)
            memcpy(buffer, fake->bulk_reply, copied);
        qtd_complete(qtd, requested - (unsigned)copied, 0);
    } else {
        copied = minimum(requested, sizeof(fake->bulk_out));
        if (copied != 0)
            memcpy(fake->bulk_out, buffer, copied);
        fake->bulk_out_length = copied;
        qtd_complete(qtd, requested - (unsigned)copied, 0);
    }
    qh->qh_qtd.qtd_next = EHCI_LINK_TERMINATE;
    fake->regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_INT;
}

static void
fake_run_async(struct fake_ehci *fake)
{
    struct ehci_qh *head;
    struct ehci_qh *qh;
    struct ehci_qtd *qtd;
    unsigned int head_phys;
    unsigned int qh_phys;
    unsigned int qtd_phys;

    if (fake->hold)
        return;
    head_phys = fake->regs[FAKE_OP(EHCI_ASYNCLISTADDR)];
    head = phys_to_ptr(head_phys);
    if (head == 0)
        return;
    qh_phys = EHCI_LINK_ADDR(head->qh_link);
    if (qh_phys == head_phys)
        return;
    qh = phys_to_ptr(qh_phys);
    if (qh == 0)
        return;
    qtd_phys = EHCI_LINK_ADDR(qh->qh_qtd.qtd_next);
    qtd = phys_to_ptr(qtd_phys);
    if (qtd == 0 || (qtd->qtd_status & EHCI_QTD_ACTIVE) == 0)
        return;
    if (qtd_pid(qtd) == EHCI_QTD_PID_SETUP)
        fake_run_control(fake, qh, qtd);
    else
        fake_run_bulk(fake, qh, qtd);
}

static void
fake_run_periodic(struct fake_ehci *fake, unsigned frame)
{
    unsigned int *frame_list;
    struct ehci_qh *qh;
    struct ehci_qtd *qtd;
    unsigned char *buffer;
    unsigned int link;
    unsigned int qtd_phys;
    unsigned completion_status;
    unsigned requested;
    unsigned visited;

    frame_list = phys_to_ptr(fake->regs[FAKE_OP(EHCI_PERIODICLISTBASE)]);
    if (frame_list == 0)
        return;
    link = frame_list[frame & (EHCI_FRAME_LIST_COUNT - 1u)];
    for (visited = 0; visited < EHCI_INTR_SLOTS &&
        (link & EHCI_LINK_TERMINATE) == 0; ++visited) {
        qh = phys_to_ptr(EHCI_LINK_ADDR(link));
        if (qh == 0)
            return;
        link = qh->qh_link;
        if ((qh->qh_qtd.qtd_status & EHCI_QTD_ACTIVE) != 0 &&
            qh->qh_curqtd != 0)
            qtd_phys = EHCI_LINK_ADDR(qh->qh_curqtd);
        else
            qtd_phys = EHCI_LINK_ADDR(qh->qh_qtd.qtd_next);
        qtd = phys_to_ptr(qtd_phys);
        if (qtd == 0 || (qtd->qtd_status & EHCI_QTD_ACTIVE) == 0)
            continue;
        requested = qtd_bytes(qtd);
        buffer = requested == 0 ? 0 : phys_to_ptr(qtd->qtd_buffer[0]);
        if (requested != 0 && buffer == 0)
            return;
        if (requested != 0)
            memset(buffer, (int)(0x40u + visited), requested);
        completion_status = fake->periodic_status;
        qtd_complete(qtd, 0, completion_status);
        fake->periodic_status = 0;
        qh->qh_curqtd = qtd_phys;
        qh->qh_qtd.qtd_next = EHCI_LINK_TERMINATE;
        qh->qh_qtd.qtd_status = qtd->qtd_status;
        ++fake->periodic_lists;
        fake->regs[FAKE_OP(EHCI_USBSTS)] |= completion_status != 0 ?
            EHCI_STS_ERRINT : EHCI_STS_INT;
    }
}

static unsigned int
fake_read(void *arg, unsigned reg)
{
    struct fake_ehci *fake;

    fake = arg;
    return fake->regs[reg / 4u];
}

static void
fake_port_write(struct fake_ehci *fake, unsigned int value)
{
    unsigned int old;
    unsigned int current;
    unsigned int rw;

    old = fake->regs[FAKE_PORT];
    rw = EHCI_PS_PO | EHCI_PS_PP | EHCI_PS_PR | EHCI_PS_SUSP |
        EHCI_PS_PE;
    current = (old & ~rw) | (value & rw);
    if (value & EHCI_PS_CSC)
        current &= ~EHCI_PS_CSC;
    if (value & EHCI_PS_PEC)
        current &= ~EHCI_PS_PEC;
    if (value & EHCI_PS_OCC)
        current &= ~EHCI_PS_OCC;
    if ((value & EHCI_PS_PR) != 0)
        fake->reset_active = 1;
    else if (fake->reset_active) {
        fake->reset_active = 0;
        fake->reset_released_at = fake->delay_total;
        current &= ~EHCI_PS_PR;
        if (fake->port_speed == USB_SPEED_HIGH &&
            (current & EHCI_PS_CS) != 0)
            current |= EHCI_PS_PE | EHCI_PS_PEC;
        else
            current &= ~EHCI_PS_PE;
    }
    fake->regs[FAKE_PORT] = current;
}

static void
fake_write(void *arg, unsigned reg, unsigned int value)
{
    struct fake_ehci *fake;
    unsigned int *status;

    fake = arg;
    status = &fake->regs[FAKE_OP(EHCI_USBSTS)];
    if (reg == FAKE_CAP_LENGTH + EHCI_USBCMD) {
        if ((fake->regs[reg / 4u] & EHCI_CMD_PSE) == 0 &&
            (value & EHCI_CMD_PSE) != 0)
            ++fake->periodic_starts;
        else if ((fake->regs[reg / 4u] & EHCI_CMD_PSE) != 0 &&
            (value & EHCI_CMD_PSE) == 0)
            ++fake->periodic_stops;
        if (value & EHCI_CMD_HCRESET) {
            fake->regs[reg / 4u] = 0;
            *status |= EHCI_STS_HCH;
            return;
        }
        fake->regs[reg / 4u] = value;
        if (value & EHCI_CMD_RS)
            *status &= ~EHCI_STS_HCH;
        else
            *status |= EHCI_STS_HCH;
        if (value & EHCI_CMD_ASE)
            *status |= EHCI_STS_ASS;
        else
            *status &= ~EHCI_STS_ASS;
        if (value & EHCI_CMD_PSE)
            *status |= EHCI_STS_PSS;
        else
            *status &= ~EHCI_STS_PSS;
        if ((value & (EHCI_CMD_RS | EHCI_CMD_ASE)) ==
            (EHCI_CMD_RS | EHCI_CMD_ASE))
            fake_run_async(fake);
        return;
    }
    if (reg == FAKE_CAP_LENGTH + EHCI_USBSTS) {
        *status &= ~value;
        return;
    }
    if (reg == FAKE_CAP_LENGTH + EHCI_PORTSC(1)) {
        fake_port_write(fake, value);
        return;
    }
    fake->regs[reg / 4u] = value;
}

static void
fake_delay(void *arg, unsigned milliseconds)
{
    struct fake_ehci *fake;

    fake = arg;
    fake->delay_total += milliseconds;
}

static void
fake_init(struct fake_ehci *fake, unsigned speed)
{
    memset(fake, 0, sizeof(*fake));
    fake->port_speed = speed;
    fake->regs[EHCI_CAPLENGTH / 4] =
        (0x0100u << 16) | FAKE_CAP_LENGTH;
    fake->regs[EHCI_HCSPARAMS / 4] =
        1u | (1u << 8) | (1u << 12) | 0x10u;
    fake->regs[FAKE_OP(EHCI_USBSTS)] = EHCI_STS_HCH;
    fake->regs[FAKE_PORT] = EHCI_PS_CS | EHCI_PS_PP |
        (speed == USB_SPEED_LOW ? 0x00000400u : 0);
}

struct hub_events {
    unsigned attached;
    unsigned detached;
    unsigned errors;
};

static void
hub_event(void *arg, unsigned port, enum usb_root_hub_event event,
    struct usb_device *device, usb_error_t status)
{
    struct hub_events *events;

    events = arg;
    if (port != 1)
        ++events->errors;
    if (event == USB_ROOT_HUB_EVENT_ATTACH) {
        ++events->attached;
        if (device == 0 || status != USB_STATUS_NORMAL_COMPLETION)
            ++events->errors;
    } else if (event == USB_ROOT_HUB_EVENT_DETACH) {
        ++events->detached;
    } else {
        fprintf(stderr, "root hub event %u failed: %s\n",
            (unsigned)event, usb_status_string(status));
        ++events->errors;
    }
}

struct owner_events {
    unsigned companion;
    unsigned ehci;
    unsigned speed;
};

struct xfer_events {
    unsigned callbacks;
    usb_error_t status;
};

struct repeat_events {
    unsigned callbacks;
    unsigned limit;
    usb_error_t status;
    usb_error_t submit_status;
};

static void
xfer_done(struct usb_xfer *xfer, void *arg, usb_error_t status)
{
    struct xfer_events *events;

    (void)xfer;
    events = arg;
    ++events->callbacks;
    events->status = status;
}

static void
repeat_done(struct usb_xfer *xfer, void *arg, usb_error_t status)
{
    struct repeat_events *events;

    events = arg;
    ++events->callbacks;
    events->status = status;
    if (events->callbacks < events->limit)
        events->submit_status = usb_submit_xfer(xfer);
}

static void
owner_change(void *arg, unsigned port, int companion, unsigned speed)
{
    struct owner_events *events;

    events = arg;
    if (port != 1)
        return;
    if (companion)
        ++events->companion;
    else
        ++events->ehci;
    events->speed = speed;
}

static struct ehci_pipe *
test_find_pipe(struct ehci_softc *ehci, struct usb_pipe *pipe)
{
    unsigned i;

    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (ehci->eh_pipes[i].ep_used &&
            ehci->eh_pipes[i].ep_pipe == pipe)
            return &ehci->eh_pipes[i];
    return 0;
}

static int
test_high_speed_enumeration_and_bulk(void)
{
    static const unsigned char reply1[] = { 'h', 'e', 'l', 'l', 'o' };
    static const unsigned char reply2[] = { 'o', 'k' };
    struct fake_ehci fake;
    struct ehci_softc ehci;
    struct usb_root_hub hub;
    struct usb_core core;
    struct usb_bus bus;
    struct usb_device *device;
    struct usb_interface *interface;
    struct usb_pipe *in_pipe;
    struct usb_pipe *out_pipe;
    struct usb_xfer *xfer;
    struct usb_xfer *busy_xfer;
    struct ehci_pipe *epipe;
    struct hub_events events;
    struct xfer_events busy_events;
    unsigned char buffer[5];
    size_t actlen;
    usb_error_t start_status;

    fake_init(&fake, USB_SPEED_HIGH);
    memset(&bus, 0, sizeof(bus));
    memset(&events, 0, sizeof(events));
    memset(&busy_events, 0, sizeof(busy_events));
    usb_task_system_init();
    usb_core_init(&core);
    ehci_softc_init(&ehci, fake_read, fake_write, fake_delay, &fake);
    start_status = usb_bus_start(&core, &bus, &ehci.eh_hcd,
        fake_delay, &fake);
    if (start_status != USB_STATUS_NORMAL_COMPLETION)
        fprintf(stderr, "EHCI start failed: %s\n",
            usb_status_string(start_status));
    CHECK(start_status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(ehci.eh_started && ehci.eh_revision == 0x0100 &&
        ehci.eh_nports == 1 && ehci.eh_ncomp == 1 && ehci.eh_npcomp == 1);
    CHECK((fake.regs[FAKE_OP(EHCI_PERIODICLISTBASE)] & 0xfffu) == 0);
    CHECK((fake.regs[FAKE_OP(EHCI_ASYNCLISTADDR)] & 0x1fu) == 0);
    CHECK(usb_root_hub_start(&hub, &bus, hub_event, &events) ==
        USB_STATUS_NORMAL_COMPLETION);
    device = usb_root_hub_device(&hub, 1);
    CHECK(device != 0 && device->ud_speed == USB_SPEED_HIGH);
    CHECK(device->ud_address == 1 && fake.address == 1 &&
        fake.configuration == 1 && fake.control_lists == 6);
    CHECK(fake.first_control_mpl == USB_2_MAX_CTRL_PACKET);
    CHECK(fake.bad_current_qtd == 0);
    CHECK(fake.first_control_at - fake.reset_released_at >=
        USB_PORT_RESET_RECOVERY);
    CHECK(fake.post_address_control_at - fake.set_address_at >=
        USB_SET_ADDRESS_SETTLE);
    CHECK(events.attached == 1 && events.errors == 0);
    CHECK((fake.regs[FAKE_PORT] & (EHCI_PS_PE | EHCI_PS_PO)) == EHCI_PS_PE);

    interface = usb_device_interface(device, 0);
    CHECK(interface != 0);
    CHECK(usb_open_pipe(interface, 0x81, &in_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_open_pipe(interface, 0x01, &out_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    epipe = test_find_pipe(&ehci, in_pipe);
    CHECK(epipe != 0 && epipe->ep_toggle == 0);
    xfer = usb_alloc_xfer(device);
    CHECK(xfer != 0);

    memset(buffer, 0, sizeof(buffer));
    fake.bulk_reply = reply1;
    fake.bulk_reply_length = sizeof(reply1);
    usb_setup_xfer(xfer, in_pipe, 0, buffer, sizeof(buffer), 0, 100, 0);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(xfer->ux_status == USB_STATUS_NORMAL_COMPLETION &&
        xfer->ux_actlen == sizeof(buffer));
    CHECK(memcmp(buffer, reply1, sizeof(buffer)) == 0);
    CHECK(fake.bulk_toggle_count == 1 && fake.bulk_toggle[0] == 0 &&
        epipe->ep_toggle == 1);

    memset(buffer, 0, sizeof(buffer));
    fake.bulk_reply = reply2;
    fake.bulk_reply_length = sizeof(reply2);
    usb_setup_xfer(xfer, in_pipe, 0, buffer, sizeof(buffer),
        USB_XFER_SHORT_OK, 100, 0);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(xfer->ux_status == USB_STATUS_NORMAL_COMPLETION &&
        xfer->ux_actlen == sizeof(reply2));
    CHECK(memcmp(buffer, reply2, sizeof(reply2)) == 0);
    CHECK(fake.bulk_toggle_count == 2 && fake.bulk_toggle[1] == 1 &&
        epipe->ep_toggle == 0);

    fake.next_halt = EHCI_QTD_HALTED;
    usb_setup_xfer(xfer, in_pipe, 0, buffer, 1, 0, 100, 0);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(xfer->ux_status == USB_STATUS_STALLED);
    CHECK((ehci.eh_last_qtd_status[0] & EHCI_QTD_HALTED) != 0);

    memcpy(buffer, "out!", 4);
    actlen = 0;
    CHECK(usb_bulk_transfer(out_pipe, buffer, 4, 0, 100, &actlen) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(actlen == 4 && fake.bulk_out_length == 4 &&
        memcmp(fake.bulk_out, "out!", 4) == 0);

    fake.hold = 1;
    usb_setup_xfer(xfer, in_pipe, 0, buffer, 1, 0, 100, 0);
    CHECK(usb_submit_xfer(xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(xfer->ux_active && ehci.eh_active_xfer == xfer);
    busy_xfer = usb_alloc_xfer(device);
    CHECK(busy_xfer != 0);
    usb_setup_xfer(busy_xfer, in_pipe, &busy_events, buffer, 1, 0, 100,
        xfer_done);
    CHECK(usb_submit_xfer(busy_xfer) == USB_STATUS_NO_MEMORY);
    CHECK(!busy_xfer->ux_active && busy_xfer->ux_done &&
        busy_events.callbacks == 1 &&
        busy_events.status == USB_STATUS_NO_MEMORY);
    CHECK(usb_free_xfer(busy_xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_abort_xfer(xfer, USB_STATUS_TIMEOUT) == USB_STATUS_TIMEOUT);
    CHECK(!xfer->ux_active && ehci.eh_active_xfer == 0);
    CHECK(usb_abort_xfer(xfer, USB_STATUS_CANCELLED) == USB_STATUS_TIMEOUT);

    actlen = 99;
    CHECK(usb_bulk_transfer(in_pipe, buffer, 1, 0, 3, &actlen) ==
        USB_STATUS_TIMEOUT);
    CHECK(actlen == 0 && ehci.eh_active_xfer == 0);
    fake.hold = 0;

    CHECK(usb_free_xfer(xfer) == USB_STATUS_NORMAL_COMPLETION);
    usb_close_pipe(out_pipe);
    usb_close_pipe(in_pipe);
    usb_root_hub_stop(&hub);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

static int
test_companion_handoff_and_reclaim(void)
{
    struct fake_ehci fake;
    struct ehci_softc ehci;
    struct usb_root_hub hub;
    struct usb_core core;
    struct usb_bus bus;
    struct hub_events hub_events;
    struct owner_events owner_events;
    struct usb_device *device;
    usb_port_status_t status;

    fake_init(&fake, USB_SPEED_LOW);
    memset(&bus, 0, sizeof(bus));
    memset(&hub_events, 0, sizeof(hub_events));
    memset(&owner_events, 0, sizeof(owner_events));
    usb_task_system_init();
    usb_core_init(&core);
    ehci_softc_init(&ehci, fake_read, fake_write, fake_delay, &fake);
    ehci_set_owner_callback(&ehci, owner_change, &owner_events);
    CHECK(usb_bus_start(&core, &bus, &ehci.eh_hcd, fake_delay, &fake) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_start(&hub, &bus, hub_event, &hub_events) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_device(&hub, 1) == 0);
    CHECK((fake.regs[FAKE_PORT] & EHCI_PS_PO) != 0);
    CHECK(owner_events.companion == 1 && owner_events.ehci == 0 &&
        owner_events.speed == USB_SPEED_LOW);
    CHECK(ehci_root_port_status(&ehci, 1, &status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK((UGETW(status.wPortStatus) & UPS_CURRENT_CONNECT_STATUS) == 0);

    /* Companion-owned changes must still be visible and drainable. */
    fake.regs[FAKE_PORT] |= EHCI_PS_CSC | EHCI_PS_PEC;
    fake.regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_PCD;
    CHECK(ehci_intr(&ehci) == 1 && usb_task_any_pending());
    usb_task_run_pending();
    CHECK((fake.regs[FAKE_PORT] & EHCI_PS_CLEAR) == 0);
    CHECK((fake.regs[FAKE_OP(EHCI_USBINTR)] & EHCI_INTR_PCIE) != 0);
    CHECK(ehci_root_port_status(&ehci, 1, &status) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(UGETW(status.wPortChange) == 0);

    /* The OHCI detach event is the board-level signal to reclaim J23. */
    fake.regs[FAKE_PORT] = EHCI_PS_PO | EHCI_PS_PP | EHCI_PS_CSC;
    CHECK(ehci_reclaim_port(&ehci, 1) == USB_STATUS_NORMAL_COMPLETION);
    CHECK((fake.regs[FAKE_PORT] & EHCI_PS_PO) == 0);
    CHECK(owner_events.ehci == 1 && owner_events.speed == USB_SPEED_UNKNOWN);

    fake.port_speed = USB_SPEED_HIGH;
    fake.regs[FAKE_PORT] = EHCI_PS_PP | EHCI_PS_CS | EHCI_PS_CSC;
    fake.regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_PCD;
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(usb_task_any_pending());
    usb_task_run_pending();
    device = usb_root_hub_device(&hub, 1);
    CHECK(device != 0 && device->ud_speed == USB_SPEED_HIGH);
    CHECK(hub_events.attached == 1 && hub_events.errors == 0);
    CHECK((fake.regs[FAKE_PORT] & EHCI_PS_PO) == 0);

    usb_root_hub_stop(&hub);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

static int
test_periodic_split_interrupts(void)
{
    struct fake_ehci fake;
    struct ehci_softc ehci;
    struct usb_root_hub hub;
    struct usb_core core;
    struct usb_bus bus;
    struct hub_events hub_events;
    struct xfer_events parent_events;
    struct xfer_events child_disconnect_events;
    struct xfer_events bulk_disconnect_events;
    struct repeat_events child_events;
    struct usb_device *parent;
    struct usb_device *child;
    struct usb_interface *parent_interface;
    struct usb_interface *child_interface;
    struct usb_pipe *parent_pipe;
    struct usb_pipe *child_pipe;
    struct usb_pipe *bulk_pipe;
    struct usb_xfer *parent_xfer;
    struct usb_xfer *child_xfer;
    struct usb_xfer *bulk_xfer;
    struct ehci_pipe *epipe;
    struct ehci_pipe *parent_epipe;
    struct ehci_pipe *child_epipe;
    unsigned char parent_report[8];
    unsigned char child_report[8];
    unsigned char bulk_data[8];
    unsigned int endp;
    unsigned int endphub;
    unsigned periodic_starts;
    unsigned periodic_stops;

    fake_init(&fake, USB_SPEED_HIGH);
    memset(&bus, 0, sizeof(bus));
    memset(&hub_events, 0, sizeof(hub_events));
    memset(&parent_events, 0, sizeof(parent_events));
    memset(&child_disconnect_events, 0,
        sizeof(child_disconnect_events));
    memset(&bulk_disconnect_events, 0, sizeof(bulk_disconnect_events));
    memset(&child_events, 0, sizeof(child_events));
    child_events.limit = 2;
    memset(parent_report, 0, sizeof(parent_report));
    memset(child_report, 0, sizeof(child_report));
    usb_task_system_init();
    usb_core_init(&core);
    ehci_softc_init(&ehci, fake_read, fake_write, fake_delay, &fake);
    CHECK(usb_bus_start(&core, &bus, &ehci.eh_hcd,
        fake_delay, &fake) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_root_hub_start(&hub, &bus, hub_event, &hub_events) ==
        USB_STATUS_NORMAL_COMPLETION);
    parent = usb_root_hub_device(&hub, 1);
    CHECK(parent != 0 && parent->ud_speed == USB_SPEED_HIGH &&
        parent->ud_address == 1);
    CHECK(usb_device_enumerate_at(&bus, parent, 3, USB_SPEED_FULL,
        &child) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(child->ud_tt_hub_address == parent->ud_address &&
        child->ud_tt_port == 3);

    epipe = test_find_pipe(&ehci, child->ud_default_pipe);
    CHECK(epipe != 0);
    endp = epipe->ep_qh->qh_endp;
    endphub = epipe->ep_qh->qh_endphub;
    CHECK(((endp >> 12) & 3u) == EHCI_QH_SPEED_FULL &&
        (endp & EHCI_QH_CTL) != 0);
    CHECK(((endphub >> 16) & 0x7fu) == parent->ud_address &&
        ((endphub >> 23) & 0x7fu) == 3 &&
        ((endphub >> 8) & 0xffu) == 0);

    parent_interface = usb_device_interface(parent, 0);
    child_interface = usb_device_interface(child, 0);
    CHECK(parent_interface != 0 && child_interface != 0);
    CHECK(usb_open_pipe(parent_interface, 0x82, &parent_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    CHECK(usb_open_pipe(child_interface, 0x82, &child_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts);
    parent_epipe = test_find_pipe(&ehci, parent_pipe);
    child_epipe = test_find_pipe(&ehci, child_pipe);
    epipe = child_epipe;
    CHECK(parent_epipe != 0 && child_epipe != 0);
    CHECK(parent_epipe->ep_intr_period == 64 &&
        parent_epipe->ep_intr_phase == 0 &&
        parent_epipe->ep_intr_uframe == 0 &&
        child_epipe->ep_intr_period == 8 &&
        child_epipe->ep_intr_phase == 1 &&
        child_epipe->ep_intr_uframe == 1 &&
        parent_epipe->ep_periodic_linked &&
        child_epipe->ep_periodic_linked);
    CHECK((parent_epipe->ep_qh->qh_endp & 0xf0000000u) == 0);
    CHECK(phys_to_ptr(EHCI_LINK_ADDR(ehci.eh_frame_list[0])) ==
        parent_epipe->ep_qh &&
        phys_to_ptr(EHCI_LINK_ADDR(ehci.eh_frame_list[1])) ==
        child_epipe->ep_qh &&
        (ehci.eh_frame_list[8] & EHCI_LINK_TERMINATE) != 0 &&
        phys_to_ptr(EHCI_LINK_ADDR(ehci.eh_frame_list[9])) ==
        child_epipe->ep_qh);
    endp = child_epipe->ep_qh->qh_endp;
    endphub = child_epipe->ep_qh->qh_endphub;
    CHECK(((endp >> 12) & 3u) == EHCI_QH_SPEED_FULL &&
        (endp & EHCI_QH_CTL) == 0 &&
        (endp & 0xf0000000u) == 0);
    CHECK((endphub & 0xffu) == 0x02 &&
        ((endphub >> 8) & 0xffu) == 0x08 &&
        ((endphub >> 16) & 0x7fu) == parent->ud_address &&
        ((endphub >> 23) & 0x7fu) == 3);
    CHECK((fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) != 0);

    parent_xfer = usb_alloc_xfer(parent);
    child_xfer = usb_alloc_xfer(child);
    CHECK(parent_xfer != 0 && child_xfer != 0);
    usb_setup_xfer(parent_xfer, parent_pipe, &parent_events,
        parent_report, sizeof(parent_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    usb_setup_xfer(child_xfer, child_pipe, &child_events,
        child_report, sizeof(child_report), USB_XFER_SHORT_OK, 0,
        repeat_done);
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    CHECK(usb_submit_xfer(parent_xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts &&
        (fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) != 0);
    CHECK(ehci.eh_intr_slots[0].eis_xfer != 0 &&
        ehci.eh_intr_slots[1].eis_xfer != 0);
    fake_run_periodic(&fake, 0);
    CHECK(fake.periodic_lists == 1);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(parent_events.callbacks == 1 && child_events.callbacks == 0 &&
        parent_events.status == USB_STATUS_NORMAL_COMPLETION);
    fake_run_periodic(&fake, 1);
    CHECK(fake.periodic_lists == 2);
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(child_events.callbacks == 1 &&
        child_events.status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(child_events.submit_status == USB_STATUS_IN_PROGRESS &&
        child_xfer->ux_active &&
        fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts &&
        child_epipe->ep_periodic_linked &&
        child_epipe->ep_qh->qh_curqtd == 0 &&
        EHCI_LINK_ADDR(child_epipe->ep_qh->qh_qtd.qtd_next) ==
        ptr_to_phys(ehci.eh_intr_slots[1].eis_qtd));
    CHECK(parent_report[0] != 0 && child_report[0] != 0);
    fake_run_periodic(&fake, 9);
    CHECK(fake.periodic_lists == 3);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(child_events.callbacks == 2 && !child_xfer->ux_active &&
        child_events.status == USB_STATUS_NORMAL_COMPLETION &&
        !child_epipe->ep_periodic_linked &&
        parent_epipe->ep_periodic_linked);

    /* Exhausted hardware XACTERR gets a bounded software retry. */
    memset(&child_events, 0, sizeof(child_events));
    child_events.limit = 1;
    fake.periodic_status = EHCI_QTD_HALTED | EHCI_QTD_XACTERR;
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 17);
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    CHECK(ehci_intr(&ehci) == 1);
    epipe = test_find_pipe(&ehci, child_pipe);
    CHECK(epipe != 0 && epipe->ep_xacterrs == 1 &&
        child_events.callbacks == 0 && child_xfer->ux_active &&
        fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts &&
        (ehci.eh_intr_slots[1].eis_qtd->qtd_status &
        (EHCI_QTD_ACTIVE | EHCI_QTD_CERR_MASK)) ==
        (EHCI_QTD_ACTIVE | EHCI_QTD_SET_CERR(3)));
    fake_run_periodic(&fake, 25);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(child_events.callbacks == 1 && !child_xfer->ux_active &&
        child_events.status == USB_STATUS_NORMAL_COMPLETION &&
        epipe->ep_xacterrs == 0);

    /* MISSEDMICRO with CERR intact is a terminal scheduling fault. */
    memset(&child_events, 0, sizeof(child_events));
    child_events.limit = 1;
    fake.periodic_status = EHCI_QTD_HALTED | EHCI_QTD_XACTERR |
        EHCI_QTD_MISSEDMICRO;
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 41);
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(child_events.callbacks == 1 && !child_xfer->ux_active &&
        child_events.status == USB_STATUS_IO_ERROR &&
        epipe->ep_xacterrs == 0 &&
        fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts);

    /* XACTERR is sticky and non-fatal when hardware did not halt the qTD. */
    memset(&parent_events, 0, sizeof(parent_events));
    fake.periodic_status = EHCI_QTD_XACTERR;
    CHECK(usb_submit_xfer(parent_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 128);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(parent_events.callbacks == 1 && !parent_xfer->ux_active &&
        parent_events.status == USB_STATUS_NORMAL_COMPLETION);

    CHECK(usb_free_xfer(child_xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(parent_xfer) == USB_STATUS_NORMAL_COMPLETION);

    /* Reuse a split periodic slot while the high-speed hub slot remains. */
    periodic_starts = fake.periodic_starts;
    periodic_stops = fake.periodic_stops;
    usb_close_pipe(child_pipe);
    CHECK(ehci.eh_intr_slots[0].eis_pipe != 0 &&
        ehci.eh_intr_slots[1].eis_pipe == 0 &&
        fake.periodic_stops == periodic_stops &&
        fake.periodic_starts == periodic_starts &&
        (fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) != 0);
    CHECK(usb_open_pipe(child_interface, 0x82, &child_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    child_xfer = usb_alloc_xfer(child);
    CHECK(child_xfer != 0);
    memset(&child_events, 0, sizeof(child_events));
    child_events.limit = 1;
    usb_setup_xfer(child_xfer, child_pipe, &child_events,
        child_report, sizeof(child_report), USB_XFER_SHORT_OK, 0,
        repeat_done);
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 33);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(child_events.callbacks == 1 && !child_xfer->ux_active &&
        child_events.status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(child_xfer) == USB_STATUS_NORMAL_COMPLETION);
    usb_close_pipe(child_pipe);

    /* Empty and rebuild the periodic schedule, then reuse its first slot. */
    usb_close_pipe(parent_pipe);
    CHECK((fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) == 0);
    CHECK(usb_open_pipe(parent_interface, 0x82, &parent_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    parent_xfer = usb_alloc_xfer(parent);
    CHECK(parent_xfer != 0);
    memset(&parent_events, 0, sizeof(parent_events));
    usb_setup_xfer(parent_xfer, parent_pipe, &parent_events,
        parent_report, sizeof(parent_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    CHECK(usb_submit_xfer(parent_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 128);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(parent_events.callbacks == 1 && !parent_xfer->ux_active &&
        parent_events.status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(parent_xfer) == USB_STATUS_NORMAL_COMPLETION);
    usb_close_pipe(parent_pipe);
    CHECK((fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) == 0);

    /*
     * Reproduce removal of a complete high-speed hub while both its own
     * interrupt QH and a split child QH are active.  JZ4780 reports PCD,
     * transaction errors and frame rollover together; disabled status bits
     * must be drained while the root-hub task owns PCD.
     */
    CHECK(usb_open_pipe(parent_interface, 0x82, &parent_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_open_pipe(child_interface, 0x82, &child_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_open_pipe(child_interface, 0x81, &bulk_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    parent_xfer = usb_alloc_xfer(parent);
    child_xfer = usb_alloc_xfer(child);
    bulk_xfer = usb_alloc_xfer(child);
    CHECK(parent_xfer != 0 && child_xfer != 0 && bulk_xfer != 0);
    memset(&parent_events, 0, sizeof(parent_events));
    memset(&child_disconnect_events, 0,
        sizeof(child_disconnect_events));
    usb_setup_xfer(parent_xfer, parent_pipe, &parent_events,
        parent_report, sizeof(parent_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    usb_setup_xfer(child_xfer, child_pipe, &child_disconnect_events,
        child_report, sizeof(child_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    usb_setup_xfer(bulk_xfer, bulk_pipe, &bulk_disconnect_events,
        bulk_data, sizeof(bulk_data), USB_XFER_SHORT_OK, 100, xfer_done);
    CHECK(usb_submit_xfer(parent_xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    fake.hold = 1;
    CHECK(usb_submit_xfer(bulk_xfer) == USB_STATUS_IN_PROGRESS);

    fake.regs[FAKE_PORT] = EHCI_PS_PP | EHCI_PS_CSC | EHCI_PS_PEC;
    fake.regs[FAKE_OP(EHCI_USBSTS)] |= EHCI_STS_PCD;
    CHECK(ehci_intr(&ehci) == 1 && usb_task_any_pending());
    CHECK((fake.regs[FAKE_OP(EHCI_USBINTR)] & EHCI_INTR_PCIE) == 0);
    CHECK(ehci.eh_root_change_pending != 0);

    /* The same plug is already back before deferred detach gets CPU time. */
    fake.address = 0;
    fake.configuration = 0;
    fake.regs[FAKE_PORT] = EHCI_PS_PP | EHCI_PS_CS | EHCI_PS_CSC;
    fake.periodic_status = EHCI_QTD_HALTED | EHCI_QTD_XACTERR;
    fake_run_periodic(&fake, 0);
    fake.periodic_status = EHCI_QTD_HALTED | EHCI_QTD_XACTERR;
    fake_run_periodic(&fake, 1);
    qtd_complete(&ehci.eh_qtds[0], sizeof(bulk_data),
        EHCI_QTD_HALTED | EHCI_QTD_XACTERR);
    fake.regs[FAKE_OP(EHCI_USBSTS)] |=
        EHCI_STS_PCD | EHCI_STS_ERRINT | EHCI_STS_FLR;
    CHECK(ehci_intr(&ehci) == 1);
    CHECK((fake.regs[FAKE_OP(EHCI_USBSTS)] & EHCI_STS_INTRS) == 0);
    CHECK(parent_events.callbacks == 1 &&
        parent_events.status == USB_STATUS_IO_ERROR);
    CHECK(child_disconnect_events.callbacks == 1 &&
        child_disconnect_events.status == USB_STATUS_IO_ERROR);
    CHECK(bulk_disconnect_events.callbacks == 1 &&
        bulk_disconnect_events.status == USB_STATUS_IO_ERROR);
    fake.hold = 0;
    usb_task_run_pending();
    parent = usb_root_hub_device(&hub, 1);
    CHECK(parent != 0 && parent->ud_speed == USB_SPEED_HIGH &&
        parent->ud_address == 1);
    CHECK(hub_events.detached == 1 && hub_events.attached == 2 &&
        hub_events.errors == 0 && ehci.eh_root_change_pending == 0);
    CHECK((fake.regs[FAKE_OP(EHCI_USBCMD)] & EHCI_CMD_PSE) == 0);
    CHECK(ehci.eh_intr_slots[0].eis_pipe == 0 &&
        ehci.eh_intr_slots[1].eis_pipe == 0);
    CHECK(usb_free_xfer(parent_xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(child_xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(bulk_xfer) == USB_STATUS_NORMAL_COMPLETION);

    /* Prove both schedules are reusable after the rapid complete replug. */
    CHECK(usb_device_enumerate_at(&bus, parent, 3, USB_SPEED_FULL,
        &child) == USB_STATUS_NORMAL_COMPLETION);
    parent_interface = usb_device_interface(parent, 0);
    child_interface = usb_device_interface(child, 0);
    CHECK(parent_interface != 0 && child_interface != 0);
    CHECK(usb_open_pipe(parent_interface, 0x82, &parent_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_open_pipe(child_interface, 0x82, &child_pipe) ==
        USB_STATUS_NORMAL_COMPLETION);
    parent_xfer = usb_alloc_xfer(parent);
    child_xfer = usb_alloc_xfer(child);
    CHECK(parent_xfer != 0 && child_xfer != 0);
    memset(&parent_events, 0, sizeof(parent_events));
    memset(&child_disconnect_events, 0,
        sizeof(child_disconnect_events));
    usb_setup_xfer(parent_xfer, parent_pipe, &parent_events,
        parent_report, sizeof(parent_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    usb_setup_xfer(child_xfer, child_pipe, &child_disconnect_events,
        child_report, sizeof(child_report), USB_XFER_SHORT_OK, 0,
        xfer_done);
    CHECK(usb_submit_xfer(parent_xfer) == USB_STATUS_IN_PROGRESS);
    CHECK(usb_submit_xfer(child_xfer) == USB_STATUS_IN_PROGRESS);
    fake_run_periodic(&fake, 0);
    CHECK(ehci_intr(&ehci) == 1);
    fake_run_periodic(&fake, 1);
    CHECK(ehci_intr(&ehci) == 1);
    CHECK(parent_events.callbacks == 1 &&
        parent_events.status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(child_disconnect_events.callbacks == 1 &&
        child_disconnect_events.status == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(parent_xfer) == USB_STATUS_NORMAL_COMPLETION);
    CHECK(usb_free_xfer(child_xfer) == USB_STATUS_NORMAL_COMPLETION);
    usb_close_pipe(child_pipe);
    usb_close_pipe(parent_pipe);
    usb_root_hub_stop(&hub);
    usb_bus_stop(&bus);
    CHECK(dma_pool_available() == dma_pool_size());
    return 0;
}

int
main(void)
{
    int failed;

    memset(dma_pool, 0xa5, sizeof(dma_pool));
    CHECK(dma_pool_init(dma_pool, FAKE_POOL_PHYS, sizeof(dma_pool),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, 0) == 0);
    failed = test_high_speed_enumeration_and_bulk();
    if (failed == 0)
        failed = test_companion_handoff_and_reclaim();
    if (failed == 0)
        failed = test_periodic_split_interrupts();
    if (failed != 0)
        return failed;
    puts("ehci tests: ok");
    return 0;
}
