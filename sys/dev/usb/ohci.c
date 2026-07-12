/*	$NetBSD: ohci.c,v 1.157.2.1 2006/08/12 21:20:31 riz Exp $	*/

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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dev/usb/ohcivar.h>

#if defined(TARGET_LITTLE_ENDIAN) || defined(__MIPSEL__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define OHCI_NATIVE_LITTLE_ENDIAN 1
#else
#define OHCI_NATIVE_LITTLE_ENDIAN 0
static unsigned int
ohci_bswap32(unsigned int value)
{
    return ((value & 0x000000ffu) << 24) |
        ((value & 0x0000ff00u) << 8) |
        ((value & 0x00ff0000u) >> 8) |
        ((value & 0xff000000u) >> 24);
}
#endif

static unsigned int
ohci_to_le32(unsigned int value)
{
#if OHCI_NATIVE_LITTLE_ENDIAN
    return value;
#else
    return ohci_bswap32(value);
#endif
}

static unsigned int
ohci_from_le32(unsigned int value)
{
    return ohci_to_le32(value);
}

static void
ohci_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
ohci_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

static unsigned int
ohci_read(struct ohci_softc *sc, unsigned reg)
{
    return sc->oh_read_4(sc->oh_io_arg, reg);
}

static void
ohci_write(struct ohci_softc *sc, unsigned reg, unsigned int value)
{
    sc->oh_write_4(sc->oh_io_arg, reg, value);
}

static void
ohci_delay(struct ohci_softc *sc, unsigned milliseconds)
{
    if (sc->oh_delay_ms != 0)
        sc->oh_delay_ms(sc->oh_io_arg, milliseconds);
}

static unsigned int
ohci_phys(struct ohci_softc *sc, const void *vaddr)
{
    const uByte *base;
    const uByte *ptr;

    base = (const uByte *)sc->oh_schedule_dma.dm_vaddr;
    ptr = (const uByte *)vaddr;
    return sc->oh_schedule_dma.dm_paddr + (unsigned int)(ptr - base);
}

static usb_error_t
ohci_cc_status(unsigned cc)
{
    switch (cc) {
    case OHCI_CC_NO_ERROR:
        return USB_STATUS_NORMAL_COMPLETION;
    case OHCI_CC_STALL:
        return USB_STATUS_STALLED;
    case OHCI_CC_DATA_UNDERRUN:
        return USB_STATUS_SHORT_XFER;
    case OHCI_CC_NOT_ACCESSED:
        return USB_STATUS_IN_PROGRESS;
    default:
        return USB_STATUS_IO_ERROR;
    }
}

static usb_error_t ohci_hcd_start(struct usb_hcd *);
static void ohci_hcd_stop(struct usb_hcd *);
static usb_error_t ohci_hcd_open_pipe(struct usb_pipe *);
static void ohci_hcd_close_pipe(struct usb_pipe *);
static usb_error_t ohci_hcd_submit_xfer(struct usb_xfer *);
static usb_error_t ohci_hcd_abort_xfer(struct usb_xfer *);
static usb_error_t ohci_hcd_root_ctrl(struct usb_hcd *,
    const usb_device_request_t *, void *, size_t *);
static void ohci_hcd_poll(struct usb_hcd *);

static const struct usb_hcd_ops ohci_hcd_ops = {
    ohci_hcd_start,
    ohci_hcd_stop,
    ohci_hcd_open_pipe,
    ohci_hcd_close_pipe,
    ohci_hcd_submit_xfer,
    ohci_hcd_abort_xfer,
    ohci_hcd_root_ctrl,
    ohci_hcd_poll
};

void
ohci_softc_init(struct ohci_softc *sc, ohci_read_4_t read_4,
    ohci_write_4_t write_4, ohci_delay_ms_t delay_ms, void *io_arg)
{
    ohci_zero(sc, sizeof(*sc));
    sc->oh_hcd.uh_ops = &ohci_hcd_ops;
    sc->oh_hcd.uh_softc = sc;
    sc->oh_read_4 = read_4;
    sc->oh_write_4 = write_4;
    sc->oh_delay_ms = delay_ms;
    sc->oh_io_arg = io_arg;
}

static usb_error_t
ohci_hcd_start(struct usb_hcd *hcd)
{
    struct ohci_softc *sc;
    unsigned int revision;
    unsigned int control;
    unsigned int interval;
    unsigned int descriptor_a;
    unsigned i;
    int error;

    sc = (struct ohci_softc *)hcd->uh_softc;
    if (sc == 0 || sc->oh_read_4 == 0 || sc->oh_write_4 == 0 ||
        sizeof(struct ohci_hcca) != OHCI_HCCA_SIZE ||
        sizeof(struct ohci_ed) != OHCI_ED_ALIGN ||
        sizeof(struct ohci_td) != OHCI_TD_ALIGN)
        return USB_STATUS_INVALID;
    revision = ohci_read(sc, OHCI_REVISION);
    if (OHCI_REV_HI(revision) != 1)
        return USB_STATUS_UNSUPPORTED;
    sc->oh_revision = revision;

    error = dma_alloc(&sc->oh_schedule_dma, OHCI_SCHEDULE_BYTES,
        OHCI_HCCA_ALIGN,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0)
        return USB_STATUS_NO_MEMORY;
    sc->oh_hcca = (struct ohci_hcca *)((uByte *)
        sc->oh_schedule_dma.dm_vaddr + OHCI_HCCA_OFFSET);
    sc->oh_control_ed = (struct ohci_ed *)((uByte *)
        sc->oh_schedule_dma.dm_vaddr + OHCI_ED_OFFSET);
    sc->oh_tds = (struct ohci_td *)((uByte *)
        sc->oh_schedule_dma.dm_vaddr + OHCI_TD_OFFSET);
    sc->oh_setup_buffer = (uByte *)sc->oh_schedule_dma.dm_vaddr +
        OHCI_SETUP_OFFSET;
    sc->oh_data_buffer = (uByte *)sc->oh_schedule_dma.dm_vaddr +
        OHCI_DATA_OFFSET;

    control = ohci_read(sc, OHCI_CONTROL);
    if (control & OHCI_IR)
        ohci_write(sc, OHCI_COMMAND_STATUS, OHCI_OCR);
    ohci_write(sc, OHCI_CONTROL, control & ~OHCI_HCFS_MASK);
    ohci_write(sc, OHCI_COMMAND_STATUS, OHCI_HCR);
    for (i = 0; i < 100; ++i) {
        if ((ohci_read(sc, OHCI_COMMAND_STATUS) & OHCI_HCR) == 0)
            break;
        ohci_delay(sc, 1);
    }
    if (i == 100) {
        (void)dma_free(&sc->oh_schedule_dma);
        return USB_STATUS_TIMEOUT;
    }

    descriptor_a = ohci_read(sc, OHCI_RH_DESCRIPTOR_A);
    sc->oh_nports = OHCI_GET_NDP(descriptor_a);
    if (sc->oh_nports == 0 || sc->oh_nports > 15) {
        (void)dma_free(&sc->oh_schedule_dma);
        return USB_STATUS_IO_ERROR;
    }
    interval = ohci_read(sc, OHCI_FM_INTERVAL);
    interval = OHCI_GET_IVAL(interval);
    if (interval == 0)
        interval = OHCI_DEFAULT_FI;
    ohci_write(sc, OHCI_INTERRUPT_DISABLE, OHCI_MIE | OHCI_ALL_INTRS);
    ohci_write(sc, OHCI_INTERRUPT_STATUS, OHCI_ALL_INTRS);
    ohci_write(sc, OHCI_HCCA, ohci_phys(sc, sc->oh_hcca));
    ohci_write(sc, OHCI_CONTROL_HEAD_ED, 0);
    ohci_write(sc, OHCI_BULK_HEAD_ED, 0);
    ohci_write(sc, OHCI_FM_INTERVAL,
        interval | OHCI_FSMPS(interval));
    ohci_write(sc, OHCI_PERIODIC_START, OHCI_PERIODIC(interval));
    ohci_write(sc, OHCI_LS_THRESHOLD, OHCI_DEFAULT_LS_THRESHOLD);
    if (dma_sync_for_device(&sc->oh_schedule_dma, 0,
        OHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0) {
        (void)dma_free(&sc->oh_schedule_dma);
        return USB_STATUS_IO_ERROR;
    }
    control = OHCI_RATIO_1_4 | OHCI_HCFS_OPERATIONAL;
    ohci_write(sc, OHCI_CONTROL, control);
    ohci_delay(sc, 10);
    sc->oh_started = 1;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
ohci_hcd_stop(struct usb_hcd *hcd)
{
    struct ohci_softc *sc;

    sc = (struct ohci_softc *)hcd->uh_softc;
    if (sc == 0 || !sc->oh_started)
        return;
    ohci_write(sc, OHCI_INTERRUPT_DISABLE, OHCI_MIE | OHCI_ALL_INTRS);
    ohci_write(sc, OHCI_CONTROL, OHCI_HCFS_RESET);
    sc->oh_active_xfer = 0;
    (void)dma_free(&sc->oh_schedule_dma);
    sc->oh_hcca = 0;
    sc->oh_control_ed = 0;
    sc->oh_tds = 0;
    sc->oh_started = 0;
}

static struct ohci_pipe *
ohci_find_pipe(struct ohci_softc *sc, struct usb_pipe *pipe)
{
    unsigned i;

    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (sc->oh_pipes[i].op_used && sc->oh_pipes[i].op_pipe == pipe)
            return &sc->oh_pipes[i];
    return 0;
}

static usb_error_t
ohci_hcd_open_pipe(struct usb_pipe *pipe)
{
    struct ohci_softc *sc;
    unsigned i;

    sc = (struct ohci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    if (ohci_find_pipe(sc, pipe) != 0)
        return USB_STATUS_INVALID;
    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (!sc->oh_pipes[i].op_used) {
            sc->oh_pipes[i].op_used = 1;
            sc->oh_pipes[i].op_pipe = pipe;
            return USB_STATUS_NORMAL_COMPLETION;
        }
    return USB_STATUS_NO_MEMORY;
}

static void
ohci_hcd_close_pipe(struct usb_pipe *pipe)
{
    struct ohci_softc *sc;
    struct ohci_pipe *opipe;

    sc = (struct ohci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    opipe = ohci_find_pipe(sc, pipe);
    if (opipe != 0)
        ohci_zero(opipe, sizeof(*opipe));
}

static usb_error_t
ohci_hcd_submit_xfer(struct usb_xfer *xfer)
{
    struct ohci_softc *sc;
    struct ohci_ed *ed;
    struct ohci_td *setup;
    struct ohci_td *data;
    struct ohci_td *status;
    struct ohci_td *tail;
    unsigned int ed_flags;
    unsigned int setup_phys;
    unsigned int data_phys;
    unsigned int status_phys;
    unsigned int tail_phys;
    unsigned int max_packet;
    unsigned int control;
    int data_in;

    sc = (struct ohci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    if (!sc->oh_started || ohci_find_pipe(sc, xfer->ux_pipe) == 0)
        return USB_STATUS_INVALID;
    if (sc->oh_active_xfer != 0)
        return USB_STATUS_IN_PROGRESS;
    if (!xfer->ux_is_control)
        return USB_STATUS_UNSUPPORTED;
    if (xfer->ux_length > OHCI_CONTROL_DATA_MAX)
        return USB_STATUS_NO_MEMORY;

    ed = sc->oh_control_ed;
    setup = &sc->oh_tds[0];
    data = &sc->oh_tds[1];
    status = &sc->oh_tds[2];
    tail = &sc->oh_tds[3];
    ohci_zero(ed, sizeof(*ed));
    ohci_zero(sc->oh_tds, sizeof(struct ohci_td) * OHCI_TD_COUNT);
    ohci_copy(sc->oh_setup_buffer, &xfer->ux_request,
        sizeof(xfer->ux_request));
    data_in = (xfer->ux_request.bmRequestType & UT_READ) != 0;
    if (xfer->ux_length != 0 && !data_in)
        ohci_copy(sc->oh_data_buffer, xfer->ux_buffer, xfer->ux_length);

    setup_phys = ohci_phys(sc, setup);
    data_phys = ohci_phys(sc, data);
    status_phys = ohci_phys(sc, status);
    tail_phys = ohci_phys(sc, tail);
    setup->td_flags = ohci_to_le32(OHCI_TD_NOCC | OHCI_TD_SETUP |
        OHCI_TD_TOGGLE_0 | OHCI_TD_NOINTR);
    setup->td_cbp = ohci_to_le32(ohci_phys(sc, sc->oh_setup_buffer));
    setup->td_nexttd = ohci_to_le32(xfer->ux_length != 0 ?
        data_phys : status_phys);
    setup->td_be = ohci_to_le32(ohci_phys(sc, sc->oh_setup_buffer) +
        sizeof(xfer->ux_request) - 1);
    if (xfer->ux_length != 0) {
        data->td_flags = ohci_to_le32(OHCI_TD_NOCC | OHCI_TD_R |
            (data_in ? OHCI_TD_IN : OHCI_TD_OUT) |
            OHCI_TD_TOGGLE_1 | OHCI_TD_NOINTR);
        data->td_cbp = ohci_to_le32(ohci_phys(sc, sc->oh_data_buffer));
        data->td_nexttd = ohci_to_le32(status_phys);
        data->td_be = ohci_to_le32(ohci_phys(sc, sc->oh_data_buffer) +
            (unsigned)xfer->ux_length - 1);
    }
    status->td_flags = ohci_to_le32(OHCI_TD_NOCC |
        (data_in ? OHCI_TD_OUT : OHCI_TD_IN) | OHCI_TD_TOGGLE_1);
    status->td_nexttd = ohci_to_le32(tail_phys);

    max_packet = UGETW(xfer->ux_pipe->up_endpoint->
        ue_desc.wMaxPacketSize) & 0x07ffu;
    ed_flags = OHCI_ED_SET_FA(xfer->ux_device->ud_address) |
        OHCI_ED_SET_EN(UE_GET_ADDR(xfer->ux_pipe->up_endpoint->
        ue_desc.bEndpointAddress)) | OHCI_ED_DIR_TD |
        OHCI_ED_FORMAT_GEN | OHCI_ED_SET_MAXP(max_packet);
    if (xfer->ux_device->ud_speed == USB_SPEED_LOW)
        ed_flags |= OHCI_ED_SPEED;
    ed->ed_flags = ohci_to_le32(ed_flags);
    ed->ed_tailp = ohci_to_le32(tail_phys);
    ed->ed_headp = ohci_to_le32(setup_phys);
    ed->ed_nexted = 0;

    sc->oh_active_xfer = xfer;
    sc->oh_active_data_length = (unsigned)xfer->ux_length;
    sc->oh_active_data_in = data_in;
    if (dma_sync_for_device(&sc->oh_schedule_dma, 0,
        OHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0) {
        sc->oh_active_xfer = 0;
        return USB_STATUS_IO_ERROR;
    }
    ohci_write(sc, OHCI_INTERRUPT_STATUS, OHCI_WDH);
    ohci_write(sc, OHCI_CONTROL_HEAD_ED, ohci_phys(sc, ed));
    control = ohci_read(sc, OHCI_CONTROL);
    ohci_write(sc, OHCI_CONTROL, control | OHCI_CLE |
        OHCI_HCFS_OPERATIONAL);
    ohci_write(sc, OHCI_COMMAND_STATUS, OHCI_CLF);
    return USB_STATUS_IN_PROGRESS;
}

static usb_error_t
ohci_hcd_abort_xfer(struct usb_xfer *xfer)
{
    struct ohci_softc *sc;
    unsigned int flags;

    sc = (struct ohci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    if (sc->oh_active_xfer != xfer)
        return USB_STATUS_INVALID;
    flags = ohci_from_le32(sc->oh_control_ed->ed_flags);
    sc->oh_control_ed->ed_flags = ohci_to_le32(flags | OHCI_ED_SKIP);
    (void)dma_sync_for_device(&sc->oh_schedule_dma, OHCI_ED_OFFSET,
        sizeof(*sc->oh_control_ed), DMA_TO_DEVICE);
    ohci_write(sc, OHCI_CONTROL_HEAD_ED, 0);
    sc->oh_active_xfer = 0;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
ohci_hcd_root_ctrl(struct usb_hcd *hcd,
    const usb_device_request_t *request, void *buffer, size_t *length)
{
    (void)hcd;
    (void)request;
    (void)buffer;
    (void)length;
    return USB_STATUS_UNSUPPORTED;
}

static void
ohci_hcd_poll(struct usb_hcd *hcd)
{
    struct ohci_softc *sc;
    struct usb_xfer *xfer;
    unsigned int head;
    unsigned int tail;
    unsigned int cbp;
    unsigned int data_start;
    unsigned cc;
    size_t actlen;
    usb_error_t result;

    sc = (struct ohci_softc *)hcd->uh_softc;
    xfer = sc->oh_active_xfer;
    if (xfer == 0)
        return;
    if (dma_sync_for_cpu(&sc->oh_schedule_dma, 0,
        OHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        return;
    head = ohci_from_le32(sc->oh_control_ed->ed_headp) &
        OHCI_ED_HEADMASK;
    tail = ohci_from_le32(sc->oh_control_ed->ed_tailp);
    if (head != tail)
        return;

    result = USB_STATUS_NORMAL_COMPLETION;
    cc = OHCI_TD_GET_CC(ohci_from_le32(sc->oh_tds[0].td_flags));
    if (cc != OHCI_CC_NO_ERROR)
        result = ohci_cc_status(cc);
    if (result == USB_STATUS_NORMAL_COMPLETION &&
        sc->oh_active_data_length != 0) {
        cc = OHCI_TD_GET_CC(ohci_from_le32(sc->oh_tds[1].td_flags));
        if (cc != OHCI_CC_NO_ERROR && cc != OHCI_CC_DATA_UNDERRUN)
            result = ohci_cc_status(cc);
    }
    if (result == USB_STATUS_NORMAL_COMPLETION) {
        cc = OHCI_TD_GET_CC(ohci_from_le32(sc->oh_tds[2].td_flags));
        if (cc != OHCI_CC_NO_ERROR)
            result = ohci_cc_status(cc);
    }

    actlen = sc->oh_active_data_length;
    if (sc->oh_active_data_length != 0) {
        cbp = ohci_from_le32(sc->oh_tds[1].td_cbp);
        data_start = ohci_phys(sc, sc->oh_data_buffer);
        if (cbp >= data_start &&
            cbp <= data_start + sc->oh_active_data_length)
            actlen = cbp - data_start;
        if (sc->oh_active_data_in && actlen != 0)
            ohci_copy(xfer->ux_buffer, sc->oh_data_buffer, actlen);
    }
    sc->oh_active_xfer = 0;
    ohci_write(sc, OHCI_CONTROL_HEAD_ED, 0);
    usb_xfer_complete(xfer, result, actlen);
}

usb_error_t
ohci_root_port_status(struct ohci_softc *sc, unsigned port,
    usb_port_status_t *status)
{
    unsigned int value;
    unsigned int port_status;
    unsigned int port_change;

    if (sc == 0 || status == 0 || !sc->oh_started || port == 0 ||
        port > sc->oh_nports)
        return USB_STATUS_INVALID;
    value = ohci_read(sc, OHCI_RH_PORT_STATUS(port));
    port_status = 0;
    port_change = 0;
    if (value & OHCI_RHPS_CCS)
        port_status |= UPS_CURRENT_CONNECT_STATUS;
    if (value & OHCI_RHPS_PES)
        port_status |= UPS_PORT_ENABLED;
    if (value & OHCI_RHPS_PSS)
        port_status |= UPS_SUSPEND;
    if (value & OHCI_RHPS_POCI)
        port_status |= UPS_OVERCURRENT_INDICATOR;
    if (value & OHCI_RHPS_PRS)
        port_status |= UPS_RESET;
    if (value & OHCI_RHPS_PPS)
        port_status |= UPS_PORT_POWER;
    if (value & OHCI_RHPS_LSDA)
        port_status |= UPS_LOW_SPEED;
    if (value & OHCI_RHPS_CSC)
        port_change |= UPS_C_CONNECT_STATUS;
    if (value & OHCI_RHPS_PESC)
        port_change |= UPS_C_PORT_ENABLED;
    if (value & OHCI_RHPS_PSSC)
        port_change |= UPS_C_SUSPEND;
    if (value & OHCI_RHPS_OCIC)
        port_change |= UPS_C_OVERCURRENT_INDICATOR;
    if (value & OHCI_RHPS_PRSC)
        port_change |= UPS_C_PORT_RESET;
    USETW(status->wPortStatus, port_status);
    USETW(status->wPortChange, port_change);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ohci_root_port_power(struct ohci_softc *sc, unsigned port, int on)
{
    unsigned int descriptor_a;
    unsigned delay_ms;

    if (sc == 0 || !sc->oh_started || port == 0 || port > sc->oh_nports)
        return USB_STATUS_INVALID;
    ohci_write(sc, OHCI_RH_PORT_STATUS(port),
        on ? OHCI_RHPS_SPP : OHCI_RHPS_CPP);
    if (on) {
        descriptor_a = ohci_read(sc, OHCI_RH_DESCRIPTOR_A);
        delay_ms = OHCI_POTPGT(descriptor_a) * 2u;
        ohci_delay(sc, delay_ms == 0 ? 2 : delay_ms);
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ohci_root_port_reset(struct ohci_softc *sc, unsigned port)
{
    unsigned int value;
    unsigned i;

    if (sc == 0 || !sc->oh_started || port == 0 || port > sc->oh_nports)
        return USB_STATUS_INVALID;
    value = ohci_read(sc, OHCI_RH_PORT_STATUS(port));
    if ((value & OHCI_RHPS_CCS) == 0)
        return USB_STATUS_DISCONNECTED;
    ohci_write(sc, OHCI_RH_PORT_STATUS(port), OHCI_RHPS_SPRS);
    for (i = 0; i < 100; ++i) {
        ohci_delay(sc, 1);
        value = ohci_read(sc, OHCI_RH_PORT_STATUS(port));
        if ((value & OHCI_RHPS_PRS) == 0 &&
            (value & OHCI_RHPS_PRSC) != 0) {
            ohci_write(sc, OHCI_RH_PORT_STATUS(port), OHCI_RHPS_PRSC);
            return USB_STATUS_NORMAL_COMPLETION;
        }
    }
    return USB_STATUS_TIMEOUT;
}
