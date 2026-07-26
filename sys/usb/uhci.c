/*	$NetBSD: uhci.c,v 1.320 2025/04/26 07:06:53 skrll Exp $	*/

/*
 * Copyright (c) 1998, 2004, 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Machine-independent UHCI host controller driver.  The register model,
 * frame list, queue-head ordering, TD construction, reset sequence and
 * root-port handling follow the NetBSD UHCI driver and the UHCI 1.1
 * specification.  ReBSD uses its common fixed-pool USB and DMA contracts:
 * one synchronous control/bulk transfer is active at a time, bulk transfers
 * are advanced through bounded schedule chunks, and interrupt-IN has an
 * independent periodic queue head.
 */

#include <usb/uhcivar.h>

#ifdef KERNEL
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>

void timeout(void (*)(caddr_t), caddr_t, int);
void untimeout(void (*)(caddr_t), caddr_t);
#endif

#if defined(TARGET_LITTLE_ENDIAN) || defined(__MIPSEL__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define UHCI_NATIVE_LITTLE_ENDIAN 1
#else
#define UHCI_NATIVE_LITTLE_ENDIAN 0
static unsigned int
uhci_bswap32(unsigned int value)
{
    return ((value & 0x000000ffu) << 24) |
        ((value & 0x0000ff00u) << 8) |
        ((value & 0x00ff0000u) >> 8) |
        ((value & 0xff000000u) >> 24);
}
#endif

static unsigned int
uhci_to_le32(unsigned int value)
{
#if UHCI_NATIVE_LITTLE_ENDIAN
    return value;
#else
    return uhci_bswap32(value);
#endif
}

static unsigned int
uhci_from_le32(unsigned int value)
{
    return uhci_to_le32(value);
}

static void
uhci_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
uhci_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

static unsigned short
uhci_read_2(struct uhci_softc *sc, unsigned reg)
{
    return sc->uh_read_2(sc->uh_io_arg, reg);
}

static void
uhci_write_2(struct uhci_softc *sc, unsigned reg, unsigned short value)
{
    sc->uh_write_2(sc->uh_io_arg, reg, value);
}

static unsigned int
uhci_read_4(struct uhci_softc *sc, unsigned reg)
{
    return sc->uh_read_4(sc->uh_io_arg, reg);
}

static void
uhci_write_4(struct uhci_softc *sc, unsigned reg, unsigned int value)
{
    sc->uh_write_4(sc->uh_io_arg, reg, value);
}

static void
uhci_delay(struct uhci_softc *sc, unsigned milliseconds)
{
    if (sc->uh_delay_ms != 0)
        sc->uh_delay_ms(sc->uh_io_arg, milliseconds);
}

static unsigned int
uhci_phys(struct uhci_softc *sc, const void *vaddr)
{
    const uByte *base;
    const uByte *ptr;

    base = (const uByte *)sc->uh_schedule_dma.dm_vaddr;
    ptr = (const uByte *)vaddr;
    return sc->uh_schedule_dma.dm_paddr + (unsigned int)(ptr - base);
}

static size_t
uhci_dma_offset(struct uhci_softc *sc, const void *vaddr)
{
    return (size_t)((const uByte *)vaddr -
        (const uByte *)sc->uh_schedule_dma.dm_vaddr);
}

static usb_error_t uhci_hcd_start(struct usb_hcd *);
static void uhci_hcd_stop(struct usb_hcd *);
static usb_error_t uhci_hcd_open_pipe(struct usb_pipe *);
static void uhci_hcd_close_pipe(struct usb_pipe *);
static usb_error_t uhci_hcd_submit_xfer(struct usb_xfer *);
static usb_error_t uhci_hcd_abort_xfer(struct usb_xfer *);
static usb_error_t uhci_hcd_root_ctrl(struct usb_hcd *,
    const usb_device_request_t *, void *, size_t *);
static void uhci_hcd_poll(struct usb_hcd *);
static unsigned uhci_hcd_root_port_count(struct usb_hcd *);
static usb_error_t uhci_hcd_root_port_status(struct usb_hcd *, unsigned,
    usb_port_status_t *);
static usb_error_t uhci_hcd_root_port_power(struct usb_hcd *, unsigned, int);
static usb_error_t uhci_hcd_root_port_reset(struct usb_hcd *, unsigned);
static usb_error_t uhci_hcd_root_port_clear_change(struct usb_hcd *,
    unsigned, unsigned);
static void uhci_hcd_root_intr_enable(struct usb_hcd *, int);
static void uhci_hcd_clear_toggle(struct usb_pipe *);
#ifdef KERNEL
static void uhci_watchdog(caddr_t);
#endif

static const struct usb_hcd_ops uhci_hcd_ops = {
    uhci_hcd_start,
    uhci_hcd_stop,
    uhci_hcd_open_pipe,
    uhci_hcd_close_pipe,
    uhci_hcd_submit_xfer,
    uhci_hcd_abort_xfer,
    uhci_hcd_root_ctrl,
    uhci_hcd_poll,
    uhci_hcd_root_port_count,
    uhci_hcd_root_port_status,
    uhci_hcd_root_port_power,
    uhci_hcd_root_port_reset,
    uhci_hcd_root_port_clear_change,
    uhci_hcd_root_intr_enable,
    uhci_hcd_clear_toggle
};

void
uhci_softc_init(struct uhci_softc *sc, uhci_read_2_t read_2,
    uhci_write_2_t write_2, uhci_read_4_t read_4,
    uhci_write_4_t write_4, uhci_delay_ms_t delay_ms, void *io_arg)
{
    uhci_zero(sc, sizeof(*sc));
    sc->uh_hcd.uh_ops = &uhci_hcd_ops;
    sc->uh_hcd.uh_softc = sc;
    sc->uh_read_2 = read_2;
    sc->uh_write_2 = write_2;
    sc->uh_read_4 = read_4;
    sc->uh_write_4 = write_4;
    sc->uh_delay_ms = delay_ms;
    sc->uh_io_arg = io_arg;
}

static int
uhci_wait_halted(struct uhci_softc *sc, int halted)
{
    unsigned i;
    unsigned value;

    for (i = 0; i < 10u; ++i) {
        value = uhci_read_2(sc, UHCI_STS);
        if (((value & UHCI_STS_HCH) != 0) == (halted != 0))
            return 0;
        uhci_delay(sc, 1);
    }
    return -1;
}

static void
uhci_schedule_init(struct uhci_softc *sc)
{
    unsigned int control_phys;
    unsigned int bulk_phys;
    unsigned int last_phys;
    unsigned int dummy_phys;
    unsigned i;

    control_phys = uhci_phys(sc, sc->uh_control_qh);
    bulk_phys = uhci_phys(sc, sc->uh_bulk_qh);
    last_phys = uhci_phys(sc, sc->uh_last_qh);
    dummy_phys = uhci_phys(sc, sc->uh_dummy_td);

    sc->uh_intr_qh->qh_hlink =
        uhci_to_le32(control_phys | UHCI_PTR_QH);
    sc->uh_intr_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_control_qh->qh_hlink =
        uhci_to_le32(bulk_phys | UHCI_PTR_QH);
    sc->uh_control_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_bulk_qh->qh_hlink =
        uhci_to_le32(last_phys | UHCI_PTR_QH);
    sc->uh_bulk_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_last_qh->qh_hlink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_last_qh->qh_elink = uhci_to_le32(dummy_phys | UHCI_PTR_TD);
    sc->uh_dummy_td->td_link = uhci_to_le32(UHCI_PTR_T);
    sc->uh_dummy_td->td_status = 0;
    sc->uh_dummy_td->td_token = 0;
    sc->uh_dummy_td->td_buffer = 0;
    for (i = 0; i < UHCI_FRAME_LIST_COUNT; ++i)
        sc->uh_frame_list[i] =
            uhci_to_le32(control_phys | UHCI_PTR_QH);
}

static void
uhci_schedule_interrupt(struct uhci_softc *sc, unsigned interval)
{
    unsigned int control_phys;
    unsigned int intr_phys;
    unsigned i;

    control_phys = uhci_phys(sc, sc->uh_control_qh);
    intr_phys = uhci_phys(sc, sc->uh_intr_qh);
    for (i = 0; i < UHCI_FRAME_LIST_COUNT; ++i)
        sc->uh_frame_list[i] = uhci_to_le32(
            interval != 0 && (i & (interval - 1u)) == 0 ?
            intr_phys | UHCI_PTR_QH : control_phys | UHCI_PTR_QH);
}

static usb_error_t
uhci_hcd_start(struct usb_hcd *hcd)
{
    struct uhci_softc *sc;
    unsigned i;
    int error;

    sc = (struct uhci_softc *)hcd->uh_softc;
    if (sc == 0 || sc->uh_read_2 == 0 || sc->uh_write_2 == 0 ||
        sc->uh_read_4 == 0 || sc->uh_write_4 == 0 ||
        sizeof(struct uhci_td) != UHCI_TD_ALIGN ||
        sizeof(struct uhci_qh) != UHCI_QH_ALIGN)
        return USB_STATUS_INVALID;
    error = dma_alloc(&sc->uh_schedule_dma, UHCI_SCHEDULE_BYTES,
        UHCI_FRAME_LIST_ALIGN,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0)
        return USB_STATUS_NO_MEMORY;

    sc->uh_frame_list = (unsigned int *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_FRAME_LIST_OFFSET);
    sc->uh_intr_qh = (struct uhci_qh *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_INTR_QH_OFFSET);
    sc->uh_control_qh = (struct uhci_qh *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_CONTROL_QH_OFFSET);
    sc->uh_bulk_qh = (struct uhci_qh *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_BULK_QH_OFFSET);
    sc->uh_last_qh = (struct uhci_qh *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_LAST_QH_OFFSET);
    sc->uh_tds = (struct uhci_td *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_TD_OFFSET);
    sc->uh_intr_td = (struct uhci_td *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_INTR_TD_OFFSET);
    sc->uh_dummy_td = (struct uhci_td *)((uByte *)
        sc->uh_schedule_dma.dm_vaddr + UHCI_DUMMY_TD_OFFSET);
    sc->uh_setup_buffer = (uByte *)sc->uh_schedule_dma.dm_vaddr +
        UHCI_SETUP_OFFSET;
    sc->uh_intr_buffer = (uByte *)sc->uh_schedule_dma.dm_vaddr +
        UHCI_INTR_BUFFER_OFFSET;
    sc->uh_data_buffer = (uByte *)sc->uh_schedule_dma.dm_vaddr +
        UHCI_DATA_OFFSET;

    uhci_write_2(sc, UHCI_INTR, 0);
    uhci_write_2(sc, UHCI_STS,
        uhci_read_2(sc, UHCI_STS) & UHCI_STS_ACK);
    uhci_write_2(sc, UHCI_CMD, UHCI_CMD_GRESET);
    uhci_delay(sc, 50);
    uhci_write_2(sc, UHCI_CMD, 0);
    uhci_delay(sc, 10);
    uhci_write_2(sc, UHCI_CMD, UHCI_CMD_HCRESET);
    for (i = 0; i < 100u; ++i) {
        if ((uhci_read_2(sc, UHCI_CMD) & UHCI_CMD_HCRESET) == 0)
            break;
        uhci_delay(sc, 1);
    }
    if (i == 100u)
        goto timeout;

    uhci_schedule_init(sc);
    if (dma_sync_for_device(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        goto io_error;
    uhci_write_2(sc, UHCI_FRNUM, 0);
    uhci_write_4(sc, UHCI_FLBASEADDR,
        uhci_phys(sc, sc->uh_frame_list));
    if ((uhci_read_4(sc, UHCI_FLBASEADDR) & UHCI_PTR_MASK) !=
        uhci_phys(sc, sc->uh_frame_list))
        goto io_error;
    uhci_write_2(sc, UHCI_STS,
        uhci_read_2(sc, UHCI_STS) & UHCI_STS_ACK);
    uhci_write_2(sc, UHCI_CMD,
        UHCI_CMD_CF | UHCI_CMD_MAXP | UHCI_CMD_RS);
    if (uhci_wait_halted(sc, 0) != 0)
        goto timeout;
    uhci_write_2(sc, UHCI_INTR, UHCI_INTR_ALL);
    sc->uh_started = 1;
    return USB_STATUS_NORMAL_COMPLETION;

timeout:
    error = USB_STATUS_TIMEOUT;
    goto fail;
io_error:
    error = USB_STATUS_IO_ERROR;
fail:
    uhci_write_2(sc, UHCI_INTR, 0);
    uhci_write_2(sc, UHCI_CMD, 0);
    (void)dma_free(&sc->uh_schedule_dma);
    return (usb_error_t)error;
}

static void
uhci_hcd_stop(struct usb_hcd *hcd)
{
    struct uhci_softc *sc;

    sc = (struct uhci_softc *)hcd->uh_softc;
    if (sc == 0 || !sc->uh_started)
        return;
#ifdef KERNEL
    if (sc->uh_watchdog_armed) {
        untimeout(uhci_watchdog, (caddr_t)sc);
        sc->uh_watchdog_armed = 0;
    }
#endif
    uhci_write_2(sc, UHCI_INTR, 0);
    uhci_write_2(sc, UHCI_CMD,
        uhci_read_2(sc, UHCI_CMD) & ~UHCI_CMD_RS);
    (void)uhci_wait_halted(sc, 1);
    sc->uh_active_xfer = 0;
    sc->uh_active_pipe = 0;
    sc->uh_intr_xfer = 0;
    sc->uh_intr_pipe = 0;
    (void)dma_free(&sc->uh_schedule_dma);
    sc->uh_frame_list = 0;
    sc->uh_intr_qh = 0;
    sc->uh_control_qh = 0;
    sc->uh_bulk_qh = 0;
    sc->uh_last_qh = 0;
    sc->uh_tds = 0;
    sc->uh_intr_td = 0;
    sc->uh_dummy_td = 0;
    sc->uh_setup_buffer = 0;
    sc->uh_intr_buffer = 0;
    sc->uh_data_buffer = 0;
    sc->uh_started = 0;
}

static struct uhci_pipe *
uhci_find_pipe(struct uhci_softc *sc, struct usb_pipe *pipe)
{
    unsigned i;

    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (sc->uh_pipes[i].up_used &&
            sc->uh_pipes[i].up_pipe == pipe)
            return &sc->uh_pipes[i];
    return 0;
}

static usb_error_t
uhci_hcd_open_pipe(struct usb_pipe *pipe)
{
    struct uhci_softc *sc;
    unsigned type;
    unsigned i;

    sc = (struct uhci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    if (uhci_find_pipe(sc, pipe) != 0 ||
        pipe->up_device->ud_speed == USB_SPEED_HIGH)
        return USB_STATUS_INVALID;
    type = UE_GET_XFERTYPE(pipe->up_endpoint->ue_desc.bmAttributes);
    if (type != UE_CONTROL && type != UE_BULK && type != UE_INTERRUPT)
        return USB_STATUS_UNSUPPORTED;
    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (!sc->uh_pipes[i].up_used) {
            sc->uh_pipes[i].up_used = 1;
            sc->uh_pipes[i].up_pipe = pipe;
            return USB_STATUS_NORMAL_COMPLETION;
        }
    return USB_STATUS_NO_MEMORY;
}

static void
uhci_hcd_close_pipe(struct usb_pipe *pipe)
{
    struct uhci_softc *sc;
    struct uhci_pipe *upipe;

    sc = (struct uhci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    upipe = uhci_find_pipe(sc, pipe);
    if (upipe != 0)
        uhci_zero(upipe, sizeof(*upipe));
}

static void
uhci_hcd_clear_toggle(struct usb_pipe *pipe)
{
    struct uhci_softc *sc;
    struct uhci_pipe *upipe;

    sc = (struct uhci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    upipe = uhci_find_pipe(sc, pipe);
    if (upipe != 0)
        upipe->up_toggle = 0;
}

static unsigned int
uhci_td_status(struct usb_device *device, int short_detect)
{
    unsigned int status;

    status = UHCI_TD_ZERO_ACTLEN | UHCI_TD_SET_ERRCNT(3) |
        UHCI_TD_ACTIVE;
    if (device->ud_speed == USB_SPEED_LOW)
        status |= UHCI_TD_LS;
    if (short_detect)
        status |= UHCI_TD_SPD;
    return status;
}

static unsigned int
uhci_td_token(unsigned pid, unsigned length, unsigned endpoint,
    unsigned address, unsigned toggle)
{
    return pid | UHCI_TD_SET_MAXLEN(length) |
        UHCI_TD_SET_ENDPT(endpoint) | UHCI_TD_SET_ADDR(address) |
        UHCI_TD_SET_TOGGLE(toggle);
}

static void
uhci_td_set(struct uhci_softc *sc, unsigned index, unsigned pid,
    unsigned length, unsigned endpoint, unsigned address, unsigned toggle,
    unsigned int status, uByte *buffer)
{
    struct uhci_td *td;
    unsigned int next;

    td = &sc->uh_tds[index];
    next = index + 1u < UHCI_TD_COUNT ?
        uhci_phys(sc, &sc->uh_tds[index + 1u]) |
        UHCI_PTR_TD | UHCI_PTR_VF : UHCI_PTR_T;
    td->td_link = uhci_to_le32(next);
    td->td_status = uhci_to_le32(status);
    td->td_token = uhci_to_le32(uhci_td_token(pid, length,
        endpoint, address, toggle));
    td->td_buffer = uhci_to_le32(length != 0 ?
        uhci_phys(sc, buffer) : 0);
}

static usb_error_t
uhci_submit_control(struct uhci_softc *sc, struct uhci_pipe *upipe,
    struct usb_xfer *xfer)
{
    unsigned int status;
    unsigned address;
    unsigned endpoint;
    unsigned max_packet;
    unsigned data_in;
    unsigned length;
    unsigned offset;
    unsigned chunk;
    unsigned toggle;
    unsigned index;

    length = (unsigned)xfer->ux_length;
    if (!xfer->ux_is_control || length > UHCI_CONTROL_DATA_MAX ||
        UGETW(xfer->ux_request.wLength) != length ||
        (length != 0 && xfer->ux_buffer == 0))
        return USB_STATUS_INVALID;
    max_packet = UGETW(xfer->ux_pipe->up_endpoint->
        ue_desc.wMaxPacketSize) & 0x07ffu;
    if (max_packet == 0)
        return USB_STATUS_INVALID;
    address = xfer->ux_device->ud_address;
    endpoint = UE_GET_ADDR(xfer->ux_pipe->up_endpoint->
        ue_desc.bEndpointAddress);
    data_in = (xfer->ux_request.bmRequestType & UT_READ) != 0;
    uhci_copy(sc->uh_setup_buffer, &xfer->ux_request,
        sizeof(xfer->ux_request));
    if (length != 0 && !data_in)
        uhci_copy(sc->uh_data_buffer, xfer->ux_buffer, length);
    uhci_zero(sc->uh_tds, sizeof(*sc->uh_tds) * UHCI_TD_COUNT);

    status = uhci_td_status(xfer->ux_device, 0);
    uhci_td_set(sc, 0, UHCI_TD_PID_SETUP,
        sizeof(xfer->ux_request), endpoint, address, 0, status,
        sc->uh_setup_buffer);
    index = 1;
    offset = 0;
    toggle = 1;
    while (offset < length) {
        chunk = length - offset;
        if (chunk > max_packet)
            chunk = max_packet;
        if (index + 1u >= UHCI_TD_COUNT)
            return USB_STATUS_NO_MEMORY;
        uhci_td_set(sc, index,
            data_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
            chunk, endpoint, address, toggle,
            uhci_td_status(xfer->ux_device, data_in),
            sc->uh_data_buffer + offset);
        toggle ^= 1u;
        offset += chunk;
        ++index;
    }
    sc->uh_active_status_td = index;
    uhci_td_set(sc, index,
        data_in ? UHCI_TD_PID_OUT : UHCI_TD_PID_IN,
        0, endpoint, address, 1,
        uhci_td_status(xfer->ux_device, 0) | UHCI_TD_IOC, 0);
    sc->uh_tds[index].td_link = uhci_to_le32(UHCI_PTR_T);
    ++index;

    sc->uh_active_xfer = xfer;
    sc->uh_active_pipe = upipe;
    sc->uh_active_td_count = index;
    sc->uh_active_offset = 0;
    sc->uh_active_chunk = length;
    sc->uh_active_data_in = data_in;
    sc->uh_active_control = 1;
    sc->uh_active_control_short = 0;
    sc->uh_control_qh->qh_elink =
        uhci_to_le32(uhci_phys(sc, &sc->uh_tds[0]) | UHCI_PTR_TD);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
uhci_build_bulk_chunk(struct uhci_softc *sc)
{
    struct usb_xfer *xfer;
    struct uhci_pipe *upipe;
    unsigned address;
    unsigned endpoint;
    unsigned max_packet;
    unsigned remaining;
    unsigned capacity;
    unsigned chunk;
    unsigned td_length;
    unsigned toggle;
    unsigned data_in;
    unsigned offset;
    unsigned i;

    xfer = sc->uh_active_xfer;
    upipe = sc->uh_active_pipe;
    max_packet = UGETW(xfer->ux_pipe->up_endpoint->
        ue_desc.wMaxPacketSize) & 0x07ffu;
    if (max_packet == 0)
        return USB_STATUS_INVALID;
    remaining = (unsigned)xfer->ux_length - sc->uh_active_offset;
    capacity = max_packet * UHCI_TD_COUNT;
    chunk = remaining;
    if (chunk > UHCI_DATA_CHUNK_MAX)
        chunk = UHCI_DATA_CHUNK_MAX;
    if (chunk > capacity)
        chunk = capacity;
    data_in = UE_GET_DIR(xfer->ux_pipe->up_endpoint->
        ue_desc.bEndpointAddress) == UE_DIR_IN;
    if (chunk != 0 && !data_in)
        uhci_copy(sc->uh_data_buffer,
            (uByte *)xfer->ux_buffer + sc->uh_active_offset, chunk);
    uhci_zero(sc->uh_tds, sizeof(*sc->uh_tds) * UHCI_TD_COUNT);
    address = xfer->ux_device->ud_address;
    endpoint = UE_GET_ADDR(xfer->ux_pipe->up_endpoint->
        ue_desc.bEndpointAddress);
    toggle = upipe->up_toggle;
    offset = 0;
    i = 0;
    do {
        td_length = chunk - offset;
        if (td_length > max_packet)
            td_length = max_packet;
        uhci_td_set(sc, i,
            data_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
            td_length, endpoint, address, toggle,
            uhci_td_status(xfer->ux_device,
            data_in || (xfer->ux_flags & USB_XFER_SHORT_OK) != 0),
            sc->uh_data_buffer + offset);
        toggle ^= 1u;
        offset += td_length;
        ++i;
    } while (offset < chunk);
    sc->uh_tds[i - 1u].td_status = uhci_to_le32(
        uhci_from_le32(sc->uh_tds[i - 1u].td_status) | UHCI_TD_IOC);
    sc->uh_tds[i - 1u].td_link = uhci_to_le32(UHCI_PTR_T);
    sc->uh_active_td_count = i;
    sc->uh_active_chunk = chunk;
    sc->uh_active_data_in = data_in;
    sc->uh_active_control = 0;
    sc->uh_active_control_short = 0;
    sc->uh_bulk_qh->qh_elink =
        uhci_to_le32(uhci_phys(sc, &sc->uh_tds[0]) | UHCI_PTR_TD);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
uhci_submit_bulk(struct uhci_softc *sc, struct uhci_pipe *upipe,
    struct usb_xfer *xfer)
{
    if (xfer->ux_is_control || xfer->ux_length > UHCI_BULK_DATA_MAX ||
        (xfer->ux_length != 0 && xfer->ux_buffer == 0))
        return USB_STATUS_INVALID;
    sc->uh_active_xfer = xfer;
    sc->uh_active_pipe = upipe;
    sc->uh_active_offset = 0;
    return uhci_build_bulk_chunk(sc);
}

static unsigned
uhci_normalize_interval(unsigned requested)
{
    unsigned interval;

    interval = 1;
    while (interval < UHCI_FRAME_LIST_COUNT &&
        (interval << 1) <= requested)
        interval <<= 1;
    return interval;
}

static usb_error_t
uhci_submit_interrupt(struct uhci_softc *sc, struct uhci_pipe *upipe,
    struct usb_xfer *xfer)
{
    struct usb_endpoint *endpoint;
    unsigned int status;
    unsigned max_packet;
    unsigned data_in;
    unsigned interval;

    if (sc->uh_intr_xfer != 0)
        return USB_STATUS_IN_PROGRESS;
    endpoint = xfer->ux_pipe->up_endpoint;
    max_packet = UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
    data_in = UE_GET_DIR(endpoint->ue_desc.bEndpointAddress) == UE_DIR_IN;
    if (!data_in || xfer->ux_length == 0 ||
        xfer->ux_length > max_packet ||
        xfer->ux_length > UHCI_INTR_DATA_MAX)
        return USB_STATUS_INVALID;
    interval = uhci_normalize_interval(endpoint->ue_desc.bInterval);
    uhci_zero(sc->uh_intr_buffer, UHCI_INTR_DATA_MAX);
    status = uhci_td_status(xfer->ux_device, 1) | UHCI_TD_IOC;
    sc->uh_intr_td->td_link = uhci_to_le32(UHCI_PTR_T);
    sc->uh_intr_td->td_status = uhci_to_le32(status);
    sc->uh_intr_td->td_token = uhci_to_le32(uhci_td_token(
        UHCI_TD_PID_IN, (unsigned)xfer->ux_length,
        UE_GET_ADDR(endpoint->ue_desc.bEndpointAddress),
        xfer->ux_device->ud_address, upipe->up_toggle));
    sc->uh_intr_td->td_buffer =
        uhci_to_le32(uhci_phys(sc, sc->uh_intr_buffer));
    sc->uh_intr_qh->qh_elink =
        uhci_to_le32(uhci_phys(sc, sc->uh_intr_td) | UHCI_PTR_TD);
    uhci_schedule_interrupt(sc, interval);
    sc->uh_intr_xfer = xfer;
    sc->uh_intr_pipe = upipe;
    sc->uh_intr_length = (unsigned)xfer->ux_length;
    sc->uh_intr_interval = interval;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
uhci_hcd_submit_xfer(struct usb_xfer *xfer)
{
    struct uhci_softc *sc;
    struct uhci_pipe *upipe;
    unsigned type;
    usb_error_t status;

    sc = (struct uhci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    upipe = uhci_find_pipe(sc, xfer->ux_pipe);
    if (!sc->uh_started || upipe == 0)
        return USB_STATUS_INVALID;
    type = UE_GET_XFERTYPE(xfer->ux_pipe->up_endpoint->
        ue_desc.bmAttributes);
    if (type == UE_INTERRUPT) {
        status = uhci_submit_interrupt(sc, upipe, xfer);
    } else {
        if (type != UE_CONTROL && type != UE_BULK)
            return USB_STATUS_UNSUPPORTED;
        if (sc->uh_active_xfer != 0)
            return USB_STATUS_NO_MEMORY;
        status = type == UE_CONTROL ?
            uhci_submit_control(sc, upipe, xfer) :
            uhci_submit_bulk(sc, upipe, xfer);
    }
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return status;
    if (dma_sync_for_device(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0) {
        if (type == UE_INTERRUPT) {
            sc->uh_intr_xfer = 0;
            sc->uh_intr_pipe = 0;
            sc->uh_intr_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
            uhci_schedule_interrupt(sc, 0);
        } else {
            sc->uh_active_xfer = 0;
            sc->uh_active_pipe = 0;
            sc->uh_control_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
            sc->uh_bulk_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
        }
        return USB_STATUS_IO_ERROR;
    }
#ifdef KERNEL
    if (!sc->uh_watchdog_armed) {
        sc->uh_watchdog_armed = 1;
        timeout(uhci_watchdog, (caddr_t)sc, (HZ + 9) / 10);
    }
#endif
    return USB_STATUS_IN_PROGRESS;
}

static usb_error_t
uhci_td_result(unsigned int status)
{
    if (status & UHCI_TD_ACTIVE)
        return USB_STATUS_IN_PROGRESS;
    if (status & UHCI_TD_STALLED)
        return USB_STATUS_STALLED;
    if (status & UHCI_TD_ERROR)
        return USB_STATUS_IO_ERROR;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
uhci_active_clear(struct uhci_softc *sc)
{
    sc->uh_control_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_bulk_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    sc->uh_active_xfer = 0;
    sc->uh_active_pipe = 0;
    sc->uh_active_td_count = 0;
    sc->uh_active_status_td = 0;
    sc->uh_active_offset = 0;
    sc->uh_active_chunk = 0;
    sc->uh_active_data_in = 0;
    sc->uh_active_control = 0;
    sc->uh_active_control_short = 0;
}

static void
uhci_complete_active(struct uhci_softc *sc, usb_error_t result,
    size_t actlen)
{
    struct usb_xfer *xfer;

    xfer = sc->uh_active_xfer;
    if (xfer == 0)
        return;
    uhci_active_clear(sc);
    (void)dma_sync_for_device(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
    usb_xfer_complete(xfer, result, actlen);
}

static void
uhci_poll_active(struct uhci_softc *sc)
{
    struct usb_xfer *xfer;
    struct uhci_pipe *upipe;
    unsigned int status;
    unsigned int token;
    unsigned expected;
    unsigned actlen;
    unsigned chunk_actlen;
    unsigned short_packet;
    unsigned i;
    usb_error_t result;

    xfer = sc->uh_active_xfer;
    upipe = sc->uh_active_pipe;
    if (xfer == 0 || upipe == 0)
        return;
    result = USB_STATUS_NORMAL_COMPLETION;
    chunk_actlen = 0;
    short_packet = 0;
    for (i = 0; i < sc->uh_active_td_count; ++i) {
        status = uhci_from_le32(sc->uh_tds[i].td_status);
        token = uhci_from_le32(sc->uh_tds[i].td_token);
        result = uhci_td_result(status);
        if (result == USB_STATUS_IN_PROGRESS) {
            if (sc->uh_active_control_short &&
                i < sc->uh_active_status_td)
                continue;
            return;
        }
        if (result != USB_STATUS_NORMAL_COMPLETION)
            break;
        if (UHCI_TD_GET_PID(token) != UHCI_TD_PID_SETUP &&
            (!sc->uh_active_control ||
            i != sc->uh_active_status_td)) {
            actlen = UHCI_TD_GET_ACTLEN(status);
            expected = UHCI_TD_GET_MAXLEN(token);
            chunk_actlen += actlen;
            if (actlen < expected) {
                short_packet = 1;
                if (sc->uh_active_control) {
                    if (!sc->uh_active_control_short) {
                        sc->uh_active_control_short = 1;
                        sc->uh_control_qh->qh_elink = uhci_to_le32(
                            uhci_phys(sc,
                            &sc->uh_tds[sc->uh_active_status_td]) |
                            UHCI_PTR_TD);
                        (void)dma_sync_for_device(
                            &sc->uh_schedule_dma,
                            uhci_dma_offset(sc, sc->uh_control_qh),
                            sizeof(*sc->uh_control_qh),
                            DMA_BIDIRECTIONAL);
                        return;
                    }
                    continue;
                }
                break;
            }
            if (!sc->uh_active_control)
                upipe->up_toggle = UHCI_TD_GET_TOGGLE(token) ^ 1u;
        }
    }
    if (result == USB_STATUS_IN_PROGRESS)
        return;
    if (sc->uh_active_control) {
        if (result == USB_STATUS_NORMAL_COMPLETION &&
            sc->uh_active_data_in && chunk_actlen != 0)
            uhci_copy(xfer->ux_buffer, sc->uh_data_buffer,
                chunk_actlen);
        uhci_complete_active(sc, result, chunk_actlen);
        return;
    }
    if (sc->uh_active_data_in && chunk_actlen != 0)
        uhci_copy((uByte *)xfer->ux_buffer + sc->uh_active_offset,
            sc->uh_data_buffer, chunk_actlen);
    sc->uh_active_offset += chunk_actlen;
    if (result != USB_STATUS_NORMAL_COMPLETION || short_packet ||
        sc->uh_active_offset >= xfer->ux_length) {
        uhci_complete_active(sc, result, sc->uh_active_offset);
        return;
    }
    result = uhci_build_bulk_chunk(sc);
    if (result != USB_STATUS_NORMAL_COMPLETION) {
        uhci_complete_active(sc, result, sc->uh_active_offset);
        return;
    }
    if (dma_sync_for_device(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        uhci_complete_active(sc, USB_STATUS_IO_ERROR,
            sc->uh_active_offset);
}

static void
uhci_poll_interrupt(struct uhci_softc *sc)
{
    struct usb_xfer *xfer;
    struct uhci_pipe *upipe;
    unsigned int status;
    unsigned int token;
    size_t actlen;
    usb_error_t result;

    xfer = sc->uh_intr_xfer;
    if (xfer == 0)
        return;
    status = uhci_from_le32(sc->uh_intr_td->td_status);
    result = uhci_td_result(status);
    if (result == USB_STATUS_IN_PROGRESS)
        return;
    token = uhci_from_le32(sc->uh_intr_td->td_token);
    actlen = result == USB_STATUS_NORMAL_COMPLETION ?
        UHCI_TD_GET_ACTLEN(status) : 0;
    upipe = sc->uh_intr_pipe;
    if (upipe != 0 && result == USB_STATUS_NORMAL_COMPLETION)
        upipe->up_toggle = UHCI_TD_GET_TOGGLE(token) ^ 1u;
    if (actlen != 0)
        uhci_copy(xfer->ux_buffer, sc->uh_intr_buffer, actlen);
    sc->uh_intr_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
    uhci_schedule_interrupt(sc, 0);
    sc->uh_intr_xfer = 0;
    sc->uh_intr_pipe = 0;
    sc->uh_intr_length = 0;
    sc->uh_intr_interval = 0;
    (void)dma_sync_for_device(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
    usb_xfer_complete(xfer, result, actlen);
}

static int
uhci_root_changes(struct uhci_softc *sc)
{
    unsigned value;
    unsigned port;

    if (sc->uh_reset_change != 0)
        return 1;
    for (port = 1; port <= UHCI_ROOT_PORTS; ++port) {
        value = uhci_read_2(sc, UHCI_PORTSC(port));
        if (value & UHCI_PORTSC_W1C)
            return 1;
    }
    return 0;
}

static void
uhci_poll_root(struct uhci_softc *sc)
{
    if (!sc->uh_root_intr_enabled || !uhci_root_changes(sc))
        return;
    sc->uh_root_intr_enabled = 0;
    sc->uh_root_change_pending = 1;
    if (sc->uh_hcd.uh_root_change == 0 ||
        sc->uh_hcd.uh_root_change(sc->uh_hcd.uh_root_change_arg) != 0) {
        sc->uh_root_change_pending = 0;
        sc->uh_root_intr_enabled = 1;
    }
}

static void
uhci_hcd_poll(struct usb_hcd *hcd)
{
    struct uhci_softc *sc;

    sc = (struct uhci_softc *)hcd->uh_softc;
    if (sc == 0 || !sc->uh_started)
        return;
    if ((sc->uh_active_xfer != 0 || sc->uh_intr_xfer != 0) &&
        dma_sync_for_cpu(&sc->uh_schedule_dma, 0,
        UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) == 0) {
        uhci_poll_interrupt(sc);
        uhci_poll_active(sc);
    }
    uhci_poll_root(sc);
}

#ifdef KERNEL
static void
uhci_watchdog(caddr_t arg)
{
    struct uhci_softc *sc;

    sc = (struct uhci_softc *)arg;
    sc->uh_watchdog_armed = 0;
    if (!sc->uh_started)
        return;
    uhci_hcd_poll(&sc->uh_hcd);
    if (sc->uh_started &&
        (sc->uh_active_xfer != 0 || sc->uh_intr_xfer != 0 ||
        sc->uh_root_intr_enabled)) {
        sc->uh_watchdog_armed = 1;
        timeout(uhci_watchdog, (caddr_t)sc, (HZ + 9) / 10);
    }
}
#endif

static usb_error_t
uhci_hcd_abort_xfer(struct usb_xfer *xfer)
{
    struct uhci_softc *sc;

    sc = (struct uhci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    if (sc->uh_active_xfer == xfer) {
        sc->uh_control_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
        sc->uh_bulk_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
        (void)dma_sync_for_device(&sc->uh_schedule_dma, 0,
            UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
        uhci_delay(sc, 2);
        uhci_active_clear(sc);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (sc->uh_intr_xfer == xfer) {
        sc->uh_intr_qh->qh_elink = uhci_to_le32(UHCI_PTR_T);
        uhci_schedule_interrupt(sc, 0);
        (void)dma_sync_for_device(&sc->uh_schedule_dma, 0,
            UHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
        uhci_delay(sc, 2);
        sc->uh_intr_xfer = 0;
        sc->uh_intr_pipe = 0;
        sc->uh_intr_length = 0;
        sc->uh_intr_interval = 0;
        return USB_STATUS_NORMAL_COMPLETION;
    }
    return USB_STATUS_INVALID;
}

static usb_error_t
uhci_hcd_root_ctrl(struct usb_hcd *hcd,
    const usb_device_request_t *request, void *buffer, size_t *length)
{
    (void)hcd;
    (void)request;
    (void)buffer;
    (void)length;
    return USB_STATUS_UNSUPPORTED;
}

usb_error_t
uhci_root_port_status(struct uhci_softc *sc, unsigned port,
    usb_port_status_t *status)
{
    unsigned value;
    unsigned port_status;
    unsigned port_change;

    if (sc == 0 || status == 0 || !sc->uh_started || port == 0 ||
        port > UHCI_ROOT_PORTS)
        return USB_STATUS_INVALID;
    value = uhci_read_2(sc, UHCI_PORTSC(port));
    port_status = UPS_PORT_POWER;
    port_change = 0;
    if (value & UHCI_PORTSC_CCS)
        port_status |= UPS_CURRENT_CONNECT_STATUS;
    if (value & UHCI_PORTSC_PE)
        port_status |= UPS_PORT_ENABLED;
    if (value & UHCI_PORTSC_SUSP)
        port_status |= UPS_SUSPEND;
    if (value & UHCI_PORTSC_OCI)
        port_status |= UPS_OVERCURRENT_INDICATOR;
    if (value & UHCI_PORTSC_LSDA)
        port_status |= UPS_LOW_SPEED;
    if (value & UHCI_PORTSC_CSC)
        port_change |= UPS_C_CONNECT_STATUS;
    if (value & UHCI_PORTSC_POEDC)
        port_change |= UPS_C_PORT_ENABLED;
    if (value & UHCI_PORTSC_OCIC)
        port_change |= UPS_C_OVERCURRENT_INDICATOR;
    if (sc->uh_reset_change & (1u << (port - 1u)))
        port_change |= UPS_C_PORT_RESET;
    USETW(status->wPortStatus, port_status);
    USETW(status->wPortChange, port_change);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
uhci_root_port_power(struct uhci_softc *sc, unsigned port, int on)
{
    (void)on;
    if (sc == 0 || !sc->uh_started || port == 0 ||
        port > UHCI_ROOT_PORTS)
        return USB_STATUS_INVALID;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
uhci_port_write(struct uhci_softc *sc, unsigned port,
    unsigned set, unsigned clear, unsigned acknowledge)
{
    unsigned value;

    value = uhci_read_2(sc, UHCI_PORTSC(port)) & UHCI_PORTSC_RW;
    value &= ~clear;
    value |= set;
    value |= acknowledge & UHCI_PORTSC_W1C;
    uhci_write_2(sc, UHCI_PORTSC(port), (unsigned short)value);
}

usb_error_t
uhci_root_port_reset(struct uhci_softc *sc, unsigned port)
{
    unsigned value;
    unsigned retry;

    if (sc == 0 || !sc->uh_started || port == 0 ||
        port > UHCI_ROOT_PORTS)
        return USB_STATUS_INVALID;
    value = uhci_read_2(sc, UHCI_PORTSC(port));
    if ((value & UHCI_PORTSC_CCS) == 0)
        return USB_STATUS_DISCONNECTED;
    uhci_port_write(sc, port, UHCI_PORTSC_PR, 0, 0);
    uhci_delay(sc, 50);
    uhci_port_write(sc, port, 0,
        UHCI_PORTSC_PR | UHCI_PORTSC_SUSP, 0);
    uhci_delay(sc, 1);
    uhci_port_write(sc, port, UHCI_PORTSC_PE, 0, 0);
    for (retry = 0; retry < 10u; ++retry) {
        uhci_delay(sc, 10);
        value = uhci_read_2(sc, UHCI_PORTSC(port));
        if ((value & UHCI_PORTSC_CCS) == 0)
            break;
        if (value & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)) {
            uhci_port_write(sc, port, 0, 0,
                value & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC));
            continue;
        }
        if (value & UHCI_PORTSC_PE)
            break;
        uhci_port_write(sc, port, UHCI_PORTSC_PE, 0, 0);
    }
    if (retry == 10u)
        return USB_STATUS_TIMEOUT;
    sc->uh_reset_change |= 1u << (port - 1u);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
uhci_root_port_clear_change(struct uhci_softc *sc, unsigned port,
    unsigned change)
{
    unsigned acknowledge;

    if (sc == 0 || !sc->uh_started || port == 0 ||
        port > UHCI_ROOT_PORTS)
        return USB_STATUS_INVALID;
    acknowledge = 0;
    if (change & UPS_C_CONNECT_STATUS)
        acknowledge |= UHCI_PORTSC_CSC;
    if (change & UPS_C_PORT_ENABLED)
        acknowledge |= UHCI_PORTSC_POEDC;
    if (change & UPS_C_OVERCURRENT_INDICATOR)
        acknowledge |= UHCI_PORTSC_OCIC;
    if (acknowledge != 0)
        uhci_port_write(sc, port, 0, 0, acknowledge);
    if (change & UPS_C_PORT_RESET)
        sc->uh_reset_change &= ~(1u << (port - 1u));
    return USB_STATUS_NORMAL_COMPLETION;
}

void
uhci_root_intr_enable(struct uhci_softc *sc, int on)
{
    if (sc == 0 || !sc->uh_started)
        return;
    sc->uh_root_intr_enabled = on != 0;
    if (on)
        sc->uh_root_change_pending = 0;
#ifdef KERNEL
    if (on && !sc->uh_watchdog_armed) {
        sc->uh_watchdog_armed = 1;
        timeout(uhci_watchdog, (caddr_t)sc, (HZ + 9) / 10);
    }
#endif
}

int
uhci_intr(struct uhci_softc *sc)
{
    unsigned status;

    if (sc == 0 || !sc->uh_started ||
        uhci_read_2(sc, UHCI_INTR) == 0)
        return 0;
    status = uhci_read_2(sc, UHCI_STS) & UHCI_STS_ALL;
    if ((status & UHCI_STS_ACK) == 0)
        return 0;
    uhci_write_2(sc, UHCI_STS,
        (unsigned short)(status & UHCI_STS_ACK));
    uhci_hcd_poll(&sc->uh_hcd);
    return 1;
}

static unsigned
uhci_hcd_root_port_count(struct usb_hcd *hcd)
{
    struct uhci_softc *sc;

    sc = (struct uhci_softc *)hcd->uh_softc;
    return sc != 0 && sc->uh_started ? UHCI_ROOT_PORTS : 0;
}

static usb_error_t
uhci_hcd_root_port_status(struct usb_hcd *hcd, unsigned port,
    usb_port_status_t *status)
{
    return uhci_root_port_status((struct uhci_softc *)hcd->uh_softc,
        port, status);
}

static usb_error_t
uhci_hcd_root_port_power(struct usb_hcd *hcd, unsigned port, int on)
{
    return uhci_root_port_power((struct uhci_softc *)hcd->uh_softc,
        port, on);
}

static usb_error_t
uhci_hcd_root_port_reset(struct usb_hcd *hcd, unsigned port)
{
    return uhci_root_port_reset((struct uhci_softc *)hcd->uh_softc,
        port);
}

static usb_error_t
uhci_hcd_root_port_clear_change(struct usb_hcd *hcd, unsigned port,
    unsigned change)
{
    return uhci_root_port_clear_change(
        (struct uhci_softc *)hcd->uh_softc, port, change);
}

static void
uhci_hcd_root_intr_enable(struct usb_hcd *hcd, int on)
{
    uhci_root_intr_enable((struct uhci_softc *)hcd->uh_softc, on);
}
