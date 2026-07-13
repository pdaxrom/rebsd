/*	$NetBSD: ehci.c,v 1.110.2.3 2006/10/03 19:51:15 tron Exp $	*/

/*
 * Copyright (c) 2001, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
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
 * Compact EHCI host controller driver for ReBSD.  The register model,
 * queue-head layout, qTD construction, reset sequence, and companion handoff
 * are adapted from the classic NetBSD 3.1 driver.  ReBSD deliberately uses a
 * fixed DMA schedule and one active async transfer instead of importing the
 * NetBSD pool, callout, locking, and soft-interrupt subsystems.
 */

#include <usb/ehcivar.h>

#ifdef KERNEL
#include <sys/types.h>
#include <sys/systm.h>
#endif

#if defined(TARGET_LITTLE_ENDIAN) || defined(__MIPSEL__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define EHCI_NATIVE_LITTLE_ENDIAN 1
#else
#define EHCI_NATIVE_LITTLE_ENDIAN 0
static unsigned int
ehci_bswap32(unsigned int value)
{
    return ((value & 0x000000ffu) << 24) |
        ((value & 0x0000ff00u) << 8) |
        ((value & 0x00ff0000u) >> 8) |
        ((value & 0xff000000u) >> 24);
}
#endif

static unsigned int
ehci_to_le32(unsigned int value)
{
#if EHCI_NATIVE_LITTLE_ENDIAN
    return value;
#else
    return ehci_bswap32(value);
#endif
}

static unsigned int
ehci_from_le32(unsigned int value)
{
    return ehci_to_le32(value);
}

static void
ehci_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
ehci_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

static unsigned int
ehci_read(struct ehci_softc *sc, unsigned reg)
{
    return sc->eh_read_4(sc->eh_io_arg, reg);
}

static void
ehci_write(struct ehci_softc *sc, unsigned reg, unsigned int value)
{
    sc->eh_write_4(sc->eh_io_arg, reg, value);
}

static unsigned int
ehci_op_read(struct ehci_softc *sc, unsigned reg)
{
    return ehci_read(sc, sc->eh_op_offset + reg);
}

static void
ehci_op_write(struct ehci_softc *sc, unsigned reg, unsigned int value)
{
    ehci_write(sc, sc->eh_op_offset + reg, value);
}

static void
ehci_delay(struct ehci_softc *sc, unsigned milliseconds)
{
    if (sc->eh_delay_ms != 0)
        sc->eh_delay_ms(sc->eh_io_arg, milliseconds);
}

static unsigned int
ehci_phys(struct ehci_softc *sc, const void *vaddr)
{
    const uByte *base;
    const uByte *ptr;

    base = (const uByte *)sc->eh_schedule_dma.dm_vaddr;
    ptr = (const uByte *)vaddr;
    return sc->eh_schedule_dma.dm_paddr + (unsigned int)(ptr - base);
}

static usb_error_t ehci_hcd_start(struct usb_hcd *);
static void ehci_hcd_stop(struct usb_hcd *);
static usb_error_t ehci_hcd_open_pipe(struct usb_pipe *);
static void ehci_hcd_close_pipe(struct usb_pipe *);
static usb_error_t ehci_hcd_submit_xfer(struct usb_xfer *);
static usb_error_t ehci_hcd_abort_xfer(struct usb_xfer *);
static usb_error_t ehci_hcd_root_ctrl(struct usb_hcd *,
    const usb_device_request_t *, void *, size_t *);
static void ehci_hcd_poll(struct usb_hcd *);
static unsigned ehci_hcd_root_port_count(struct usb_hcd *);
static usb_error_t ehci_hcd_root_port_status(struct usb_hcd *, unsigned,
    usb_port_status_t *);
static usb_error_t ehci_hcd_root_port_power(struct usb_hcd *, unsigned, int);
static usb_error_t ehci_hcd_root_port_reset(struct usb_hcd *, unsigned);
static usb_error_t ehci_hcd_root_port_clear_change(struct usb_hcd *,
    unsigned, unsigned);
static void ehci_hcd_root_intr_enable(struct usb_hcd *, int);
static void ehci_hcd_clear_toggle(struct usb_pipe *);

static const struct usb_hcd_ops ehci_hcd_ops = {
    ehci_hcd_start,
    ehci_hcd_stop,
    ehci_hcd_open_pipe,
    ehci_hcd_close_pipe,
    ehci_hcd_submit_xfer,
    ehci_hcd_abort_xfer,
    ehci_hcd_root_ctrl,
    ehci_hcd_poll,
    ehci_hcd_root_port_count,
    ehci_hcd_root_port_status,
    ehci_hcd_root_port_power,
    ehci_hcd_root_port_reset,
    ehci_hcd_root_port_clear_change,
    ehci_hcd_root_intr_enable,
    ehci_hcd_clear_toggle
};

void
ehci_softc_init(struct ehci_softc *sc, ehci_read_4_t read_4,
    ehci_write_4_t write_4, ehci_delay_ms_t delay_ms, void *io_arg)
{
    ehci_zero(sc, sizeof(*sc));
    sc->eh_hcd.uh_ops = &ehci_hcd_ops;
    sc->eh_hcd.uh_softc = sc;
    sc->eh_read_4 = read_4;
    sc->eh_write_4 = write_4;
    sc->eh_delay_ms = delay_ms;
    sc->eh_io_arg = io_arg;
}

void
ehci_set_owner_callback(struct ehci_softc *sc,
    ehci_owner_change_t callback, void *arg)
{
    if (sc == 0)
        return;
    sc->eh_owner_change = callback;
    sc->eh_owner_arg = arg;
}

static int
ehci_wait_status(struct ehci_softc *sc, unsigned mask, unsigned expected,
    unsigned timeout_ms)
{
    unsigned i;

    for (i = 0; i < timeout_ms; ++i) {
        if ((ehci_op_read(sc, EHCI_USBSTS) & mask) == expected)
            return 0;
        ehci_delay(sc, 1);
    }
    return -1;
}

static int
ehci_async_pause(struct ehci_softc *sc)
{
    unsigned int command;

    command = ehci_op_read(sc, EHCI_USBCMD);
    if ((command & EHCI_CMD_ASE) == 0)
        return 0;
    ehci_op_write(sc, EHCI_USBCMD, command & ~EHCI_CMD_ASE);
    return ehci_wait_status(sc, EHCI_STS_ASS, 0, 100);
}

static void
ehci_async_resume(struct ehci_softc *sc)
{
    unsigned int command;

    command = ehci_op_read(sc, EHCI_USBCMD);
    ehci_op_write(sc, EHCI_USBCMD,
        command | EHCI_CMD_ASE | EHCI_CMD_RS);
}

static int
ehci_periodic_pause(struct ehci_softc *sc)
{
    unsigned int command;

    command = ehci_op_read(sc, EHCI_USBCMD);
    if ((command & EHCI_CMD_PSE) == 0)
        return 0;
    ehci_op_write(sc, EHCI_USBCMD, command & ~EHCI_CMD_PSE);
    return ehci_wait_status(sc, EHCI_STS_PSS, 0, 100);
}

static void
ehci_periodic_resume(struct ehci_softc *sc)
{
    unsigned int command;

    command = ehci_op_read(sc, EHCI_USBCMD);
    ehci_op_write(sc, EHCI_USBCMD,
        command | EHCI_CMD_PSE | EHCI_CMD_RS);
}

static void
ehci_qh_idle(struct ehci_qh *qh)
{
    /* Current qTD has no terminate bit; its low five bits are reserved. */
    qh->qh_curqtd = 0;
    qh->qh_qtd.qtd_next = ehci_to_le32(EHCI_LINK_TERMINATE);
    qh->qh_qtd.qtd_altnext = ehci_to_le32(EHCI_LINK_TERMINATE);
    qh->qh_qtd.qtd_status = ehci_to_le32(EHCI_QTD_HALTED);
}

static usb_error_t
ehci_hcd_start(struct usb_hcd *hcd)
{
    struct ehci_softc *sc;
    unsigned int capability;
    unsigned int structural;
    unsigned int command;
    unsigned int head_phys;
    unsigned i;
    int error;

    sc = (struct ehci_softc *)hcd->uh_softc;
    if (sc == 0 || sc->eh_read_4 == 0 || sc->eh_write_4 == 0 ||
        sizeof(struct ehci_qtd) != 64u || sizeof(struct ehci_qh) != 96u ||
        EHCI_PIPE_QH_OFFSET + USB_MAX_PIPES * sizeof(struct ehci_qh) >
        EHCI_QTD_OFFSET ||
        EHCI_QTD_OFFSET + EHCI_QTD_COUNT * sizeof(struct ehci_qtd) >
        EHCI_INTR_QTD_OFFSET ||
        EHCI_INTR_QTD_OFFSET + EHCI_INTR_SLOTS * sizeof(struct ehci_qtd) >
        EHCI_INTR_BUFFER_OFFSET ||
        EHCI_INTR_BUFFER_OFFSET + EHCI_INTR_SLOTS * EHCI_INTR_DATA_MAX >
        EHCI_SETUP_OFFSET ||
        EHCI_SETUP_OFFSET + sizeof(usb_device_request_t) >
        EHCI_DATA_OFFSET)
        return USB_STATUS_INVALID;

    capability = ehci_read(sc, EHCI_CAPLENGTH);
    sc->eh_op_offset = EHCI_CAP_GET_LENGTH(capability);
    sc->eh_revision = EHCI_CAP_GET_VERSION(capability);
    if (sc->eh_op_offset < 0x10u || sc->eh_op_offset > 0x80u ||
        (sc->eh_revision >> 8) != 1u)
        return USB_STATUS_UNSUPPORTED;
    structural = ehci_read(sc, EHCI_HCSPARAMS);
    sc->eh_nports = EHCI_HCS_N_PORTS(structural);
    sc->eh_ncomp = EHCI_HCS_N_CC(structural);
    sc->eh_npcomp = EHCI_HCS_N_PCC(structural);
    sc->eh_ppc = EHCI_HCS_PPC(structural) != 0;
    if (sc->eh_nports == 0 || sc->eh_nports > EHCI_MAX_PORTS ||
        sc->eh_nports > USB_MAX_ROOT_PORTS)
        return USB_STATUS_UNSUPPORTED;

    error = dma_alloc(&sc->eh_schedule_dma, EHCI_SCHEDULE_BYTES,
        EHCI_PAGE_SIZE,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS);
    if (error != 0)
        return USB_STATUS_NO_MEMORY;
    sc->eh_frame_list = (unsigned int *)((uByte *)
        sc->eh_schedule_dma.dm_vaddr + EHCI_FRAME_LIST_OFFSET);
    sc->eh_async_head = (struct ehci_qh *)((uByte *)
        sc->eh_schedule_dma.dm_vaddr + EHCI_ASYNC_HEAD_OFFSET);
    sc->eh_pipe_qhs = (struct ehci_qh *)((uByte *)
        sc->eh_schedule_dma.dm_vaddr + EHCI_PIPE_QH_OFFSET);
    sc->eh_qtds = (struct ehci_qtd *)((uByte *)
        sc->eh_schedule_dma.dm_vaddr + EHCI_QTD_OFFSET);
    sc->eh_intr_qtds = (struct ehci_qtd *)((uByte *)
        sc->eh_schedule_dma.dm_vaddr + EHCI_INTR_QTD_OFFSET);
    sc->eh_intr_buffers = (uByte *)sc->eh_schedule_dma.dm_vaddr +
        EHCI_INTR_BUFFER_OFFSET;
    sc->eh_setup_buffer = (uByte *)sc->eh_schedule_dma.dm_vaddr +
        EHCI_SETUP_OFFSET;
    sc->eh_data_buffer = (uByte *)sc->eh_schedule_dma.dm_vaddr +
        EHCI_DATA_OFFSET;

    for (i = 0; i < EHCI_INTR_SLOTS; ++i) {
        sc->eh_intr_slots[i].eis_xfer = 0;
        sc->eh_intr_slots[i].eis_pipe = 0;
        sc->eh_intr_slots[i].eis_qtd = &sc->eh_intr_qtds[i];
        sc->eh_intr_slots[i].eis_buffer = sc->eh_intr_buffers +
            i * EHCI_INTR_DATA_MAX;
        sc->eh_intr_slots[i].eis_length = 0;
    }

    for (i = 0; i < EHCI_FRAME_LIST_COUNT; ++i)
        sc->eh_frame_list[i] = ehci_to_le32(EHCI_LINK_TERMINATE);
    head_phys = ehci_phys(sc, sc->eh_async_head);
    sc->eh_async_head->qh_link = ehci_to_le32(head_phys | EHCI_LINK_QH);
    sc->eh_async_head->qh_endp = ehci_to_le32(
        EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH) | EHCI_QH_HRECL);
    sc->eh_async_head->qh_endphub = ehci_to_le32(EHCI_QH_SET_MULT(1));
    ehci_qh_idle(sc->eh_async_head);

    ehci_op_write(sc, EHCI_USBINTR, 0);
    ehci_op_write(sc, EHCI_USBCMD, 0);
    ehci_delay(sc, 1);
    ehci_op_write(sc, EHCI_USBCMD, EHCI_CMD_HCRESET);
    for (i = 0; i < 100; ++i) {
        command = ehci_op_read(sc, EHCI_USBCMD);
        if ((command & EHCI_CMD_HCRESET) == 0)
            break;
        ehci_delay(sc, 1);
    }
    if (i == 100)
        goto timeout;

    ehci_op_write(sc, EHCI_CTRLDSSEGMENT, 0);
    ehci_op_write(sc, EHCI_PERIODICLISTBASE,
        ehci_phys(sc, sc->eh_frame_list));
    ehci_op_write(sc, EHCI_ASYNCLISTADDR, head_phys);
    ehci_op_write(sc, EHCI_USBSTS, EHCI_STS_INTRS);
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        goto io_error;
    ehci_op_write(sc, EHCI_USBINTR, EHCI_TRANSFER_INTRS);
    command = EHCI_CMD_ITC_2 | EHCI_CMD_ASE | EHCI_CMD_RS;
    ehci_op_write(sc, EHCI_USBCMD, command);
    ehci_op_write(sc, EHCI_CONFIGFLAG, EHCI_CONF_CF);
    if (ehci_wait_status(sc, EHCI_STS_HCH, 0, 100) != 0)
        goto timeout;
    sc->eh_started = 1;
    return USB_STATUS_NORMAL_COMPLETION;

timeout:
    error = USB_STATUS_TIMEOUT;
    goto bad;
io_error:
    error = USB_STATUS_IO_ERROR;
bad:
    ehci_op_write(sc, EHCI_USBINTR, 0);
    ehci_op_write(sc, EHCI_USBCMD, 0);
    (void)dma_free(&sc->eh_schedule_dma);
    sc->eh_frame_list = 0;
    sc->eh_async_head = 0;
    sc->eh_pipe_qhs = 0;
    sc->eh_qtds = 0;
    sc->eh_intr_qtds = 0;
    sc->eh_intr_buffers = 0;
    sc->eh_setup_buffer = 0;
    sc->eh_data_buffer = 0;
    return (usb_error_t)error;
}

static void
ehci_hcd_stop(struct usb_hcd *hcd)
{
    struct ehci_softc *sc;
    unsigned i;

    sc = (struct ehci_softc *)hcd->uh_softc;
    if (sc == 0 || !sc->eh_started)
        return;
    ehci_op_write(sc, EHCI_USBINTR, 0);
    ehci_op_write(sc, EHCI_USBCMD, 0);
    sc->eh_active_xfer = 0;
    sc->eh_active_pipe = 0;
    sc->eh_started = 0;
    (void)dma_free(&sc->eh_schedule_dma);
    sc->eh_frame_list = 0;
    sc->eh_async_head = 0;
    sc->eh_pipe_qhs = 0;
    sc->eh_qtds = 0;
    sc->eh_intr_qtds = 0;
    sc->eh_intr_buffers = 0;
    sc->eh_setup_buffer = 0;
    sc->eh_data_buffer = 0;
    for (i = 0; i < EHCI_INTR_SLOTS; ++i)
        ehci_zero(&sc->eh_intr_slots[i], sizeof(sc->eh_intr_slots[i]));
    for (i = 0; i < USB_MAX_PIPES; ++i)
        ehci_zero(&sc->eh_pipes[i], sizeof(sc->eh_pipes[i]));
}

static struct ehci_pipe *
ehci_find_pipe(struct ehci_softc *sc, struct usb_pipe *pipe)
{
    unsigned i;

    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (sc->eh_pipes[i].ep_used && sc->eh_pipes[i].ep_pipe == pipe)
            return &sc->eh_pipes[i];
    return 0;
}

static unsigned
ehci_interrupt_period(const struct usb_device *device, unsigned interval)
{
    unsigned period;

    if (device->ud_speed == USB_SPEED_HIGH) {
        /* High-speed bInterval is an exponent in 125-us microframes. */
        if (interval <= 4u)
            return 1u;
        if (interval - 4u >= 10u)
            return EHCI_FRAME_LIST_COUNT;
        return 1u << (interval - 4u);
    }

    /* Full/low-speed bInterval is in frames; EHCI uses power-of-two slots. */
    period = 1u;
    while (period < EHCI_FRAME_LIST_COUNT &&
        period <= interval / 2u)
        period <<= 1;
    return period;
}

static int
ehci_interrupt_frames_overlap(const struct ehci_pipe *a,
    const struct ehci_pipe *b)
{
    unsigned period;

    period = a->ep_intr_period < b->ep_intr_period ?
        a->ep_intr_period : b->ep_intr_period;
    return (a->ep_intr_phase & (period - 1u)) ==
        (b->ep_intr_phase & (period - 1u));
}

static unsigned
ehci_interrupt_uframe(struct ehci_softc *sc, struct ehci_pipe *epipe)
{
    struct ehci_pipe *other;
    unsigned limit;
    unsigned type;
    unsigned used;
    unsigned i;
    unsigned uframe;

    limit = epipe->ep_pipe->up_device->ud_speed == USB_SPEED_HIGH ?
        8u : 4u;
    used = 0;
    for (i = 0; i < USB_MAX_PIPES; ++i) {
        other = &sc->eh_pipes[i];
        if (other == epipe || !other->ep_used || other->ep_pipe == 0)
            continue;
        type = UE_GET_XFERTYPE(other->ep_pipe->up_endpoint->
            ue_desc.bmAttributes);
        if (type == UE_INTERRUPT &&
            ehci_interrupt_frames_overlap(epipe, other))
            used |= 1u << other->ep_intr_uframe;
    }
    for (uframe = 0; uframe < limit; ++uframe)
        if ((used & (1u << uframe)) == 0)
            return uframe;
    return epipe->ep_intr_slot & (limit - 1u);
}

static void
ehci_qh_configure(struct ehci_softc *sc, struct ehci_pipe *epipe,
    unsigned int first_qtd, int async)
{
    struct usb_device *device;
    struct usb_endpoint *endpoint;
    struct ehci_qh *qh;
    unsigned int endp;
    unsigned int endphub;
    unsigned int speed;
    unsigned type;
    unsigned max_packet;
    unsigned naks;
    unsigned start_uframe;

    device = epipe->ep_pipe->up_device;
    endpoint = epipe->ep_pipe->up_endpoint;
    qh = epipe->ep_qh;
    type = UE_GET_XFERTYPE(endpoint->ue_desc.bmAttributes);
    max_packet = UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
    speed = device->ud_speed == USB_SPEED_HIGH ? EHCI_QH_SPEED_HIGH :
        (device->ud_speed == USB_SPEED_LOW ? EHCI_QH_SPEED_LOW :
        EHCI_QH_SPEED_FULL);
    /* Periodic QHs must not use NAK throttling (Linux/modern NetBSD). */
    naks = type == UE_INTERRUPT ? 0u :
        (speed == EHCI_QH_SPEED_HIGH ? 4u : 0u);

    ehci_zero(qh, sizeof(*qh));
    if (async)
        qh->qh_link = ehci_to_le32(ehci_phys(sc, sc->eh_async_head) |
            EHCI_LINK_QH);
    else
        qh->qh_link = ehci_to_le32(EHCI_LINK_TERMINATE);
    endp = EHCI_QH_SET_ADDR(device->ud_address) |
        EHCI_QH_SET_ENDPT(UE_GET_ADDR(endpoint->ue_desc.bEndpointAddress)) |
        EHCI_QH_SET_EPS(speed) | EHCI_QH_DTC |
        EHCI_QH_SET_MPL(max_packet) | EHCI_QH_SET_NRL(naks);
    if (speed != EHCI_QH_SPEED_HIGH && type == UE_CONTROL)
        endp |= EHCI_QH_CTL;
    qh->qh_endp = ehci_to_le32(endp);

    endphub = EHCI_QH_SET_MULT(1);
    if (speed != EHCI_QH_SPEED_HIGH) {
        endphub |= EHCI_QH_SET_HUBA(device->ud_tt_hub_address) |
            EHCI_QH_SET_PORT(device->ud_tt_port);
        if (type == UE_INTERRUPT) {
            /*
             * Spread start-splits over Y0..Y3 and permit three consecutive
             * complete-split opportunities two microframes later.  This is
             * the same S-mask/C-mask shape selected by Linux's
             * check_intr_schedule().
             */
            start_uframe = epipe->ep_intr_uframe;
            endphub |= EHCI_QH_SET_SMASK(1u << start_uframe) |
                EHCI_QH_SET_CMASK(7u << (start_uframe + 2u));
        }
    } else if (type == UE_INTERRUPT) {
        start_uframe = epipe->ep_intr_uframe;
        endphub |= EHCI_QH_SET_SMASK(1u << start_uframe);
    }
    qh->qh_endphub = ehci_to_le32(endphub);
    qh->qh_curqtd = 0;
    qh->qh_qtd.qtd_next = ehci_to_le32(first_qtd);
    qh->qh_qtd.qtd_altnext = ehci_to_le32(EHCI_LINK_TERMINATE);
    qh->qh_qtd.qtd_status = first_qtd == EHCI_LINK_TERMINATE ?
        ehci_to_le32(EHCI_QTD_HALTED) : 0;
}

static usb_error_t
ehci_rebuild_periodic(struct ehci_softc *sc)
{
    struct ehci_pipe *scheduled[EHCI_INTR_SLOTS];
    struct ehci_pipe *epipe;
    struct ehci_pipe *next;
    unsigned type;
    unsigned count;
    unsigned frame;
    unsigned i;
    unsigned j;
    unsigned k;
    unsigned was_running;
    int sync_error;

    was_running = (ehci_op_read(sc, EHCI_USBCMD) & EHCI_CMD_PSE) != 0;
    if (ehci_periodic_pause(sc) != 0)
        return USB_STATUS_TIMEOUT;
    for (i = 0; i < EHCI_FRAME_LIST_COUNT; ++i)
        sc->eh_frame_list[i] = ehci_to_le32(EHCI_LINK_TERMINATE);
    count = 0;
    for (i = 0; i < USB_MAX_PIPES; ++i) {
        epipe = &sc->eh_pipes[i];
        if (!epipe->ep_used || epipe->ep_pipe == 0)
            continue;
        type = UE_GET_XFERTYPE(epipe->ep_pipe->up_endpoint->
            ue_desc.bmAttributes);
        if (type != UE_INTERRUPT)
            continue;
        /* Linux orders each periodic branch from slow to fast. */
        for (j = count; j != 0 &&
            scheduled[j - 1u]->ep_intr_period < epipe->ep_intr_period;
            --j)
            scheduled[j] = scheduled[j - 1u];
        scheduled[j] = epipe;
        ++count;
    }

    /*
     * Power-of-two periods make phases either disjoint or nested.  A QH's
     * horizontal link can therefore point at the first faster QH whose
     * phase is present in every frame occupied by this QH.
     */
    for (i = 0; i < count; ++i) {
        next = 0;
        for (j = i + 1u; j < count; ++j)
            if ((scheduled[i]->ep_intr_phase &
                (scheduled[j]->ep_intr_period - 1u)) ==
                scheduled[j]->ep_intr_phase) {
                next = scheduled[j];
                break;
            }
        scheduled[i]->ep_qh->qh_link = ehci_to_le32(next == 0 ?
            EHCI_LINK_TERMINATE :
            (ehci_phys(sc, next->ep_qh) | EHCI_LINK_QH));
    }
    for (frame = 0; frame < EHCI_FRAME_LIST_COUNT; ++frame)
        for (k = 0; k < count; ++k)
            if ((frame & (scheduled[k]->ep_intr_period - 1u)) ==
                scheduled[k]->ep_intr_phase) {
                sc->eh_frame_list[frame] = ehci_to_le32(
                    ehci_phys(sc, scheduled[k]->ep_qh) | EHCI_LINK_QH);
                break;
            }
    sync_error = dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
    /* Linux waits nine microframes before reusing an unlinked intr QH. */
    if (was_running)
        ehci_delay(sc, 2);
    if (count != 0)
        ehci_periodic_resume(sc);
    return sync_error == 0 ? USB_STATUS_NORMAL_COMPLETION :
        USB_STATUS_IO_ERROR;
}

static usb_error_t
ehci_hcd_open_pipe(struct usb_pipe *pipe)
{
    struct ehci_softc *sc;
    unsigned type;
    unsigned max_packet;
    unsigned i;
    unsigned slot;
    usb_error_t status;
#ifdef KERNEL
    unsigned int endp;
    unsigned int endphub;
#endif

    sc = (struct ehci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    if (!sc->eh_started || ehci_find_pipe(sc, pipe) != 0)
        return USB_STATUS_INVALID;
    if (pipe->up_device->ud_speed != USB_SPEED_HIGH &&
        (pipe->up_device->ud_tt_hub_address == 0 ||
        pipe->up_device->ud_tt_port == 0))
        return USB_STATUS_UNSUPPORTED;
    type = UE_GET_XFERTYPE(pipe->up_endpoint->ue_desc.bmAttributes);
    if (type != UE_CONTROL && type != UE_BULK && type != UE_INTERRUPT)
        return USB_STATUS_UNSUPPORTED;
    max_packet = UGETW(pipe->up_endpoint->ue_desc.wMaxPacketSize) &
        0x07ffu;
    if (max_packet == 0)
        return USB_STATUS_INVALID;
    slot = EHCI_INTR_SLOT_NONE;
    if (type == UE_INTERRUPT) {
        if (UE_GET_DIR(pipe->up_endpoint->ue_desc.bEndpointAddress) !=
            UE_DIR_IN || max_packet > EHCI_INTR_DATA_MAX)
            return USB_STATUS_UNSUPPORTED;
        for (slot = 0; slot < EHCI_INTR_SLOTS; ++slot)
            if (sc->eh_intr_slots[slot].eis_pipe == 0)
                break;
        if (slot == EHCI_INTR_SLOTS)
            return USB_STATUS_NO_MEMORY;
    }
    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (!sc->eh_pipes[i].ep_used) {
            sc->eh_pipes[i].ep_used = 1;
            sc->eh_pipes[i].ep_pipe = pipe;
            sc->eh_pipes[i].ep_qh = &sc->eh_pipe_qhs[i];
            sc->eh_pipes[i].ep_intr_slot = slot;
            if (type == UE_INTERRUPT) {
                sc->eh_pipes[i].ep_intr_period =
                    ehci_interrupt_period(pipe->up_device,
                    pipe->up_endpoint->ue_desc.bInterval);
                sc->eh_pipes[i].ep_intr_phase = slot &
                    (sc->eh_pipes[i].ep_intr_period - 1u);
                sc->eh_pipes[i].ep_intr_uframe =
                    ehci_interrupt_uframe(sc, &sc->eh_pipes[i]);
            }
            ehci_qh_configure(sc, &sc->eh_pipes[i],
                EHCI_LINK_TERMINATE, type != UE_INTERRUPT);
            if (type == UE_INTERRUPT) {
                sc->eh_intr_slots[slot].eis_pipe = &sc->eh_pipes[i];
                status = ehci_rebuild_periodic(sc);
                if (status != USB_STATUS_NORMAL_COMPLETION) {
                    sc->eh_intr_slots[slot].eis_pipe = 0;
                    ehci_zero(sc->eh_pipes[i].ep_qh,
                        sizeof(*sc->eh_pipes[i].ep_qh));
                    ehci_zero(&sc->eh_pipes[i],
                        sizeof(sc->eh_pipes[i]));
                    return status;
                }
#ifdef KERNEL
                endp = ehci_from_le32(sc->eh_pipes[i].ep_qh->qh_endp);
                endphub = ehci_from_le32(sc->eh_pipes[i].ep_qh->
                    qh_endphub);
                printf("ehci: periodic addr=%u endpoint=%x "
                    "bInterval=%u frames=%u phase=%u uframe=%u "
                    "nrl=%u smask=%x cmask=%x\n",
                    pipe->up_device->ud_address,
                    pipe->up_endpoint->ue_desc.bEndpointAddress,
                    pipe->up_endpoint->ue_desc.bInterval,
                    sc->eh_pipes[i].ep_intr_period,
                    sc->eh_pipes[i].ep_intr_phase,
                    sc->eh_pipes[i].ep_intr_uframe,
                    (endp >> 28) & 0x0fu,
                    endphub & 0xffu, (endphub >> 8) & 0xffu);
#endif
            }
            return USB_STATUS_NORMAL_COMPLETION;
        }
    return USB_STATUS_NO_MEMORY;
}

static void
ehci_hcd_close_pipe(struct usb_pipe *pipe)
{
    struct ehci_softc *sc;
    struct ehci_pipe *epipe;
    unsigned type;
    unsigned slot;

    sc = (struct ehci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    epipe = ehci_find_pipe(sc, pipe);
    if (epipe != 0) {
        type = UE_GET_XFERTYPE(pipe->up_endpoint->ue_desc.bmAttributes);
        if (type == UE_INTERRUPT) {
            slot = epipe->ep_intr_slot;
            if (slot < EHCI_INTR_SLOTS) {
                sc->eh_intr_slots[slot].eis_xfer = 0;
                sc->eh_intr_slots[slot].eis_pipe = 0;
                sc->eh_intr_slots[slot].eis_length = 0;
            }
            epipe->ep_used = 0;
            (void)ehci_rebuild_periodic(sc);
        }
        if (epipe->ep_qh != 0)
            ehci_zero(epipe->ep_qh, sizeof(*epipe->ep_qh));
        ehci_zero(epipe, sizeof(*epipe));
    }
}

static void
ehci_hcd_clear_toggle(struct usb_pipe *pipe)
{
    struct ehci_softc *sc;
    struct ehci_pipe *epipe;

    sc = (struct ehci_softc *)pipe->up_device->ud_bus->ub_hcd->uh_softc;
    epipe = ehci_find_pipe(sc, pipe);
    if (epipe != 0)
        epipe->ep_toggle = 0;
}

static void
ehci_qtd_buffer(struct ehci_softc *sc, struct ehci_qtd *qtd,
    const void *buffer, unsigned length)
{
    unsigned int address;
    unsigned int limit;
    unsigned int page;
    unsigned i;

    if (length == 0)
        return;
    address = ehci_phys(sc, buffer);
    limit = address + length - 1u;
    qtd->qtd_buffer[0] = ehci_to_le32(address);
    page = EHCI_PAGE(address) + EHCI_PAGE_SIZE;
    for (i = 1; i < EHCI_QTD_NBUFFERS && page <= limit; ++i) {
        qtd->qtd_buffer[i] = ehci_to_le32(page);
        page += EHCI_PAGE_SIZE;
    }
}

static void
ehci_qtd_init(struct ehci_softc *sc, struct ehci_qtd *qtd,
    unsigned pid, unsigned toggle, void *buffer, unsigned length,
    unsigned int next, unsigned int altnext, int ioc)
{
    unsigned int token;

    ehci_zero(qtd, sizeof(*qtd));
    qtd->qtd_next = ehci_to_le32(next);
    qtd->qtd_altnext = ehci_to_le32(altnext);
    token = EHCI_QTD_ACTIVE | EHCI_QTD_SET_CERR(3) |
        EHCI_QTD_SET_PID(pid) | EHCI_QTD_SET_BYTES(length) |
        EHCI_QTD_SET_TOGGLE(toggle);
    if (ioc)
        token |= EHCI_QTD_IOC;
    qtd->qtd_status = ehci_to_le32(token);
    ehci_qtd_buffer(sc, qtd, buffer, length);
}

static usb_error_t
ehci_qh_set_interrupt_qtd(struct ehci_softc *sc, struct ehci_qh *qh,
    unsigned int qtd_phys)
{
    unsigned i;

    /*
     * This QH remains linked in the periodic schedule.  Freeze its overlay
     * and publish that state before changing the current/next qTD fields;
     * otherwise the controller can fetch a half-updated overlay.  This is
     * the ordering used by NetBSD's ehci_set_qh_qtd().
     */
    qh->qh_qtd.qtd_status = ehci_to_le32(EHCI_QTD_HALTED);
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        return USB_STATUS_IO_ERROR;
    qh->qh_curqtd = 0;
    qh->qh_qtd.qtd_next = ehci_to_le32(qtd_phys);
    qh->qh_qtd.qtd_altnext = ehci_to_le32(EHCI_LINK_TERMINATE);
    for (i = 0; i < EHCI_QTD_NBUFFERS; ++i) {
        qh->qh_qtd.qtd_buffer[i] = 0;
        qh->qh_qtd.qtd_buffer_hi[i] = 0;
    }
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        return USB_STATUS_IO_ERROR;
    qh->qh_qtd.qtd_status = 0;
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0) {
        qh->qh_qtd.qtd_status = ehci_to_le32(EHCI_QTD_HALTED);
        return USB_STATUS_IO_ERROR;
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
ehci_qh_init(struct ehci_softc *sc, struct ehci_pipe *epipe,
    struct usb_xfer *xfer, unsigned int first_qtd)
{
    (void)xfer;
    ehci_qh_configure(sc, epipe, first_qtd, 1);
}

static usb_error_t
ehci_submit_interrupt(struct ehci_softc *sc, struct ehci_pipe *epipe,
    struct usb_xfer *xfer)
{
    struct ehci_intr_slot *slot;
    struct ehci_qh *qh;
    unsigned slot_index;
    unsigned int qtd_phys;
    usb_error_t status;

    slot_index = epipe->ep_intr_slot;
    if (slot_index >= EHCI_INTR_SLOTS || xfer->ux_is_control ||
        xfer->ux_length > EHCI_INTR_DATA_MAX ||
        (xfer->ux_length != 0 && xfer->ux_buffer == 0))
        return USB_STATUS_INVALID;
    slot = &sc->eh_intr_slots[slot_index];
    if (slot->eis_pipe != epipe || slot->eis_xfer != 0 ||
        slot->eis_qtd == 0 || slot->eis_buffer == 0)
        return USB_STATUS_NO_MEMORY;
    qtd_phys = ehci_phys(sc, slot->eis_qtd);
    ehci_qtd_init(sc, slot->eis_qtd, EHCI_QTD_PID_IN,
        epipe->ep_toggle, slot->eis_buffer, (unsigned)xfer->ux_length,
        EHCI_LINK_TERMINATE, EHCI_LINK_TERMINATE, 1);

    qh = epipe->ep_qh;
    slot->eis_xfer = xfer;
    slot->eis_length = (unsigned)xfer->ux_length;
    epipe->ep_xacterrs = 0;
    ehci_op_write(sc, EHCI_USBSTS, EHCI_STS_INT | EHCI_STS_ERRINT);
    status = ehci_qh_set_interrupt_qtd(sc, qh, qtd_phys);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        ehci_qh_idle(qh);
        slot->eis_xfer = 0;
        slot->eis_length = 0;
        return status;
    }
    return USB_STATUS_IN_PROGRESS;
}

static usb_error_t
ehci_submit_control(struct ehci_softc *sc, struct ehci_pipe *epipe,
    struct usb_xfer *xfer)
{
    struct ehci_qtd *setup;
    struct ehci_qtd *data;
    struct ehci_qtd *status;
    unsigned int setup_phys;
    unsigned int data_phys;
    unsigned int status_phys;
    unsigned int data_next;
    unsigned data_in;

    if (!xfer->ux_is_control || xfer->ux_length > EHCI_DATA_MAX ||
        UGETW(xfer->ux_request.wLength) != xfer->ux_length ||
        (xfer->ux_length != 0 && xfer->ux_buffer == 0))
        return USB_STATUS_INVALID;
    ehci_copy(&sc->eh_last_request, &xfer->ux_request,
        sizeof(sc->eh_last_request));
    sc->eh_last_address = xfer->ux_device->ud_address;
    setup = &sc->eh_qtds[0];
    data = &sc->eh_qtds[1];
    status = &sc->eh_qtds[2];
    setup_phys = ehci_phys(sc, setup);
    data_phys = ehci_phys(sc, data);
    status_phys = ehci_phys(sc, status);
    data_in = (xfer->ux_request.bmRequestType & UT_READ) != 0;
    ehci_copy(sc->eh_setup_buffer, &xfer->ux_request,
        sizeof(xfer->ux_request));
    if (xfer->ux_length != 0 && !data_in)
        ehci_copy(sc->eh_data_buffer, xfer->ux_buffer, xfer->ux_length);

    ehci_qtd_init(sc, setup, EHCI_QTD_PID_SETUP, 0,
        sc->eh_setup_buffer, sizeof(xfer->ux_request),
        xfer->ux_length != 0 ? data_phys : status_phys,
        EHCI_LINK_TERMINATE, 0);
    if (xfer->ux_length != 0) {
        data_next = data_in ? status_phys : EHCI_LINK_TERMINATE;
        ehci_qtd_init(sc, data,
            data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, 1,
            sc->eh_data_buffer, (unsigned)xfer->ux_length,
            status_phys, data_next, 0);
    } else {
        ehci_zero(data, sizeof(*data));
    }
    ehci_qtd_init(sc, status,
        data_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN, 1,
        0, 0, EHCI_LINK_TERMINATE, EHCI_LINK_TERMINATE, 1);
    ehci_zero(&sc->eh_qtds[3], sizeof(sc->eh_qtds[3]));
    ehci_qh_init(sc, epipe, xfer, setup_phys);
    sc->eh_active_qtds = 3;
    sc->eh_active_control = 1;
    sc->eh_active_data_in = data_in;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
ehci_submit_bulk(struct ehci_softc *sc, struct ehci_pipe *epipe,
    struct usb_xfer *xfer)
{
    struct usb_endpoint *endpoint;
    unsigned int qtd_phys;
    unsigned data_in;

    endpoint = xfer->ux_pipe->up_endpoint;
    if (xfer->ux_is_control || xfer->ux_length > EHCI_DATA_MAX ||
        (xfer->ux_length != 0 && xfer->ux_buffer == 0))
        return USB_STATUS_INVALID;
    data_in = UE_GET_DIR(endpoint->ue_desc.bEndpointAddress) == UE_DIR_IN;
    if (xfer->ux_length != 0 && !data_in)
        ehci_copy(sc->eh_data_buffer, xfer->ux_buffer, xfer->ux_length);
    qtd_phys = ehci_phys(sc, &sc->eh_qtds[0]);
    ehci_qtd_init(sc, &sc->eh_qtds[0],
        data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT,
        epipe->ep_toggle, sc->eh_data_buffer, (unsigned)xfer->ux_length,
        EHCI_LINK_TERMINATE, EHCI_LINK_TERMINATE, 1);
    ehci_zero(&sc->eh_qtds[1], sizeof(sc->eh_qtds[1]) * 3u);
    ehci_qh_init(sc, epipe, xfer, qtd_phys);
    sc->eh_active_qtds = 1;
    sc->eh_active_control = 0;
    sc->eh_active_data_in = data_in;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
ehci_hcd_submit_xfer(struct usb_xfer *xfer)
{
    struct ehci_softc *sc;
    struct ehci_pipe *epipe;
    unsigned int qh_phys;
    unsigned type;
    usb_error_t status;

    sc = (struct ehci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    epipe = ehci_find_pipe(sc, xfer->ux_pipe);
    if (!sc->eh_started || epipe == 0)
        return USB_STATUS_INVALID;
    type = UE_GET_XFERTYPE(xfer->ux_pipe->up_endpoint->
        ue_desc.bmAttributes);
    if (type == UE_INTERRUPT)
        return ehci_submit_interrupt(sc, epipe, xfer);
    if (type != UE_CONTROL && type != UE_BULK)
        return USB_STATUS_UNSUPPORTED;
    if (sc->eh_active_xfer != 0)
        return USB_STATUS_NO_MEMORY;
    if (ehci_async_pause(sc) != 0)
        return USB_STATUS_TIMEOUT;
    if (type == UE_CONTROL)
        status = ehci_submit_control(sc, epipe, xfer);
    else
        status = ehci_submit_bulk(sc, epipe, xfer);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        ehci_async_resume(sc);
        return status;
    }
    sc->eh_active_xfer = xfer;
    sc->eh_active_pipe = epipe;
    epipe->ep_xacterrs = 0;
    sc->eh_active_length = (unsigned)xfer->ux_length;
    sc->eh_last_qh_phys = ehci_phys(sc, epipe->ep_qh);
    sc->eh_last_qtd_phys = ehci_phys(sc, &sc->eh_qtds[0]);
    sc->eh_last_qh_status = 0;
    sc->eh_last_qtd_status[0] = 0;
    sc->eh_last_qtd_status[1] = 0;
    sc->eh_last_qtd_status[2] = 0;
    qh_phys = ehci_phys(sc, epipe->ep_qh);
    sc->eh_async_head->qh_link = ehci_to_le32(qh_phys | EHCI_LINK_QH);
    ehci_op_write(sc, EHCI_USBSTS, EHCI_STS_INT | EHCI_STS_ERRINT);
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0) {
        sc->eh_async_head->qh_link = ehci_to_le32(
            ehci_phys(sc, sc->eh_async_head) | EHCI_LINK_QH);
        sc->eh_active_xfer = 0;
        sc->eh_active_pipe = 0;
        ehci_async_resume(sc);
        return USB_STATUS_IO_ERROR;
    }
    ehci_async_resume(sc);
    return USB_STATUS_IN_PROGRESS;
}

static usb_error_t
ehci_qtd_result(unsigned int token)
{
    if (token & EHCI_QTD_ACTIVE)
        return USB_STATUS_IN_PROGRESS;
    /*
     * Transaction error bits are sticky across hardware retries.  They are
     * fatal only when the controller also halts the qTD; a completed qTD may
     * legitimately retain XACTERR after a later retry succeeds.
     */
    if (token & EHCI_QTD_HALTED) {
        if ((token & EHCI_QTD_STATERRS) != 0)
            return USB_STATUS_IO_ERROR;
        return USB_STATUS_STALLED;
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

static int
ehci_retry_xacterr(struct ehci_softc *sc, struct ehci_pipe *epipe,
    struct ehci_qtd *qtd, unsigned int token)
{
    if ((token & (EHCI_QTD_HALTED | EHCI_QTD_XACTERR)) !=
        (EHCI_QTD_HALTED | EHCI_QTD_XACTERR) ||
        EHCI_QTD_GET_CERR(token) != 0 ||
        ++epipe->ep_xacterrs >= EHCI_XACTERR_RETRY_MAX)
        return 0;

    /*
     * EHCI hardware has exhausted its three transaction attempts.  Linux
     * qh_completions() retries this condition in software because split
     * transactions can transiently exhaust CERR without losing the device.
     * Preserve the transfer state (including SPLITXSTATE and data toggle),
     * clear HALTED, and give both the qTD and QH overlay three new attempts.
     */
    token &= ~(EHCI_QTD_HALTED | EHCI_QTD_CERR_MASK);
    token |= EHCI_QTD_ACTIVE | EHCI_QTD_SET_CERR(3);
    qtd->qtd_status = ehci_to_le32(token);
    epipe->ep_qh->qh_qtd.qtd_status = ehci_to_le32(token);
    if (dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        return 0;
    return 1;
}

static void
ehci_unlink_active(struct ehci_softc *sc)
{
    unsigned int head_phys;

    if (ehci_async_pause(sc) != 0) {
        ehci_op_write(sc, EHCI_USBCMD, 0);
        ehci_delay(sc, 1);
    }
    head_phys = ehci_phys(sc, sc->eh_async_head);
    sc->eh_async_head->qh_link = ehci_to_le32(head_phys | EHCI_LINK_QH);
    if (sc->eh_active_pipe != 0 && sc->eh_active_pipe->ep_qh != 0)
        ehci_qh_idle(sc->eh_active_pipe->ep_qh);
    (void)dma_sync_for_device(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
    sc->eh_active_xfer = 0;
    sc->eh_active_pipe = 0;
    sc->eh_active_qtds = 0;
    sc->eh_active_length = 0;
    sc->eh_active_data_in = 0;
    sc->eh_active_control = 0;
    ehci_async_resume(sc);
}

static void
ehci_complete_active(struct ehci_softc *sc, usb_error_t result,
    size_t actlen)
{
    struct usb_xfer *xfer;
    unsigned i;

    xfer = sc->eh_active_xfer;
    if (xfer == 0)
        return;
    if (result != USB_STATUS_NORMAL_COMPLETION) {
        if (sc->eh_active_pipe != 0 &&
            sc->eh_active_pipe->ep_qh != 0) {
            sc->eh_last_qh_link = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_link);
            sc->eh_last_qh_endp = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_endp);
            sc->eh_last_qh_endphub = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_endphub);
            sc->eh_last_qh_curqtd = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_curqtd);
            sc->eh_last_qh_next = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_qtd.qtd_next);
            sc->eh_last_qh_altnext = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_qtd.qtd_altnext);
            sc->eh_last_qh_status = ehci_from_le32(
                sc->eh_active_pipe->ep_qh->qh_qtd.qtd_status);
        }
        for (i = 0; i < 3; ++i) {
            sc->eh_last_qtd_next[i] = ehci_from_le32(
                sc->eh_qtds[i].qtd_next);
            sc->eh_last_qtd_altnext[i] = ehci_from_le32(
                sc->eh_qtds[i].qtd_altnext);
            sc->eh_last_qtd_status[i] = ehci_from_le32(
                sc->eh_qtds[i].qtd_status);
            sc->eh_last_qtd_buffer[i] = ehci_from_le32(
                sc->eh_qtds[i].qtd_buffer[0]);
        }
        ehci_copy(sc->eh_last_setup, sc->eh_setup_buffer,
            sizeof(sc->eh_last_setup));
    }
    ehci_unlink_active(sc);
    usb_xfer_complete(xfer, result, actlen);
}

static usb_error_t
ehci_hcd_abort_xfer(struct usb_xfer *xfer)
{
    struct ehci_softc *sc;
    struct ehci_pipe *epipe;
    struct ehci_intr_slot *slot;
    unsigned type;
    unsigned was_running;

    sc = (struct ehci_softc *)xfer->ux_device->ud_bus->ub_hcd->uh_softc;
    epipe = ehci_find_pipe(sc, xfer->ux_pipe);
    if (epipe == 0)
        return USB_STATUS_INVALID;
    type = UE_GET_XFERTYPE(xfer->ux_pipe->up_endpoint->
        ue_desc.bmAttributes);
    if (type == UE_INTERRUPT) {
        if (epipe->ep_intr_slot >= EHCI_INTR_SLOTS)
            return USB_STATUS_INVALID;
        slot = &sc->eh_intr_slots[epipe->ep_intr_slot];
        if (slot->eis_xfer != xfer)
            return USB_STATUS_INVALID;
        was_running = (ehci_op_read(sc, EHCI_USBCMD) &
            EHCI_CMD_PSE) != 0;
        if (was_running && ehci_periodic_pause(sc) == 0)
            ehci_delay(sc, 2);
        ehci_qh_idle(epipe->ep_qh);
        slot->eis_xfer = 0;
        slot->eis_length = 0;
        (void)dma_sync_for_device(&sc->eh_schedule_dma, 0,
            EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
        if (was_running)
            ehci_periodic_resume(sc);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (sc->eh_active_xfer != xfer)
        return USB_STATUS_INVALID;
    ehci_unlink_active(sc);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
ehci_hcd_root_ctrl(struct usb_hcd *hcd,
    const usb_device_request_t *request, void *buffer, size_t *length)
{
    (void)hcd;
    (void)request;
    (void)buffer;
    (void)length;
    return USB_STATUS_UNSUPPORTED;
}

static void
ehci_poll_active(struct ehci_softc *sc)
{
    struct usb_xfer *xfer;
    struct ehci_pipe *epipe;
    unsigned int token;
    unsigned max_packet;
    unsigned packets;
    unsigned remaining;
    size_t actlen;
    usb_error_t result;

    xfer = sc->eh_active_xfer;
    epipe = sc->eh_active_pipe;
    if (xfer == 0 || epipe == 0)
        return;
    token = ehci_from_le32(sc->eh_qtds[0].qtd_status);
    if (ehci_retry_xacterr(sc, epipe, &sc->eh_qtds[0], token))
        return;
    result = ehci_qtd_result(token);
    if (result == USB_STATUS_IN_PROGRESS)
        return;
    actlen = 0;
    if (sc->eh_active_control) {
        if (result == USB_STATUS_NORMAL_COMPLETION &&
            sc->eh_active_length != 0) {
            token = ehci_from_le32(sc->eh_qtds[1].qtd_status);
            if (ehci_retry_xacterr(sc, epipe, &sc->eh_qtds[1], token))
                return;
            result = ehci_qtd_result(token);
            if (result == USB_STATUS_IN_PROGRESS)
                return;
            remaining = EHCI_QTD_GET_BYTES(token);
            if (remaining > sc->eh_active_length)
                result = USB_STATUS_IO_ERROR;
            else
                actlen = sc->eh_active_length - remaining;
        }
        if (result == USB_STATUS_NORMAL_COMPLETION) {
            token = ehci_from_le32(sc->eh_qtds[2].qtd_status);
            if (ehci_retry_xacterr(sc, epipe, &sc->eh_qtds[2], token))
                return;
            result = ehci_qtd_result(token);
            if (result == USB_STATUS_IN_PROGRESS)
                return;
        }
    } else {
        token = ehci_from_le32(sc->eh_qtds[0].qtd_status);
        remaining = EHCI_QTD_GET_BYTES(token);
        if (remaining > sc->eh_active_length)
            result = USB_STATUS_IO_ERROR;
        else
            actlen = sc->eh_active_length - remaining;
        if (result == USB_STATUS_NORMAL_COMPLETION) {
            max_packet = UGETW(xfer->ux_pipe->up_endpoint->
                ue_desc.wMaxPacketSize) & 0x07ffu;
            packets = actlen == 0 ? 1u :
                ((unsigned)actlen + max_packet - 1u) / max_packet;
            epipe->ep_toggle ^= packets & 1u;
        }
    }
    if (sc->eh_active_data_in && actlen != 0)
        ehci_copy(xfer->ux_buffer, sc->eh_data_buffer, actlen);
    epipe->ep_xacterrs = 0;
    ehci_complete_active(sc, result, actlen);
}

static void
ehci_poll_interrupts(struct ehci_softc *sc)
{
    struct ehci_intr_slot *slot;
    struct ehci_pipe *epipe;
    struct usb_xfer *xfer;
    unsigned int token;
    unsigned max_packet;
    unsigned packets;
    unsigned remaining;
    unsigned i;
    size_t actlen;
    usb_error_t result;

    for (i = 0; i < EHCI_INTR_SLOTS; ++i) {
        slot = &sc->eh_intr_slots[i];
        xfer = slot->eis_xfer;
        epipe = slot->eis_pipe;
        if (xfer == 0 || epipe == 0 || slot->eis_qtd == 0)
            continue;
        token = ehci_from_le32(slot->eis_qtd->qtd_status);
        if (ehci_retry_xacterr(sc, epipe, slot->eis_qtd, token))
            continue;
        result = ehci_qtd_result(token);
        if (result == USB_STATUS_IN_PROGRESS)
            continue;
        remaining = EHCI_QTD_GET_BYTES(token);
        if (remaining > slot->eis_length) {
            result = USB_STATUS_IO_ERROR;
            actlen = 0;
        } else {
            actlen = slot->eis_length - remaining;
        }
        if (result == USB_STATUS_NORMAL_COMPLETION) {
            max_packet = UGETW(xfer->ux_pipe->up_endpoint->
                ue_desc.wMaxPacketSize) & 0x07ffu;
            packets = actlen == 0 ? 1u :
                ((unsigned)actlen + max_packet - 1u) / max_packet;
            epipe->ep_toggle ^= packets & 1u;
            if (actlen != 0)
                ehci_copy(xfer->ux_buffer, slot->eis_buffer, actlen);
        }
#ifdef KERNEL
        if (result != USB_STATUS_NORMAL_COMPLETION)
            printf("ehci: periodic addr=%u endpoint=%x failed "
                "token=%x qh-status=%x endp=%x hub=%x retries=%u\n",
                xfer->ux_device->ud_address,
                xfer->ux_pipe->up_endpoint->ue_desc.bEndpointAddress,
                token, ehci_from_le32(epipe->ep_qh->qh_qtd.qtd_status),
                ehci_from_le32(epipe->ep_qh->qh_endp),
                ehci_from_le32(epipe->ep_qh->qh_endphub),
                epipe->ep_xacterrs);
#endif
        epipe->ep_xacterrs = 0;
        ehci_qh_idle(epipe->ep_qh);
        slot->eis_xfer = 0;
        slot->eis_length = 0;
        (void)dma_sync_for_device(&sc->eh_schedule_dma, 0,
            EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL);
        usb_xfer_complete(xfer, result, actlen);
    }
}

static void
ehci_hcd_poll(struct usb_hcd *hcd)
{
    struct ehci_softc *sc;

    sc = (struct ehci_softc *)hcd->uh_softc;
    if (sc == 0 || !sc->eh_started)
        return;
    if (dma_sync_for_cpu(&sc->eh_schedule_dma, 0,
        EHCI_SCHEDULE_BYTES, DMA_BIDIRECTIONAL) != 0)
        return;
    ehci_poll_interrupts(sc);
    ehci_poll_active(sc);
}

static unsigned int
ehci_port_read(struct ehci_softc *sc, unsigned port)
{
    return ehci_op_read(sc, EHCI_PORTSC(port));
}

static void
ehci_port_modify(struct ehci_softc *sc, unsigned port,
    unsigned int set, unsigned int clear)
{
    unsigned int value;

    value = ehci_port_read(sc, port);
    value &= ~EHCI_PS_CLEAR;
    value &= ~clear;
    value |= set;
    ehci_op_write(sc, EHCI_PORTSC(port), value);
}

static void
ehci_set_port_owner(struct ehci_softc *sc, unsigned port, int companion,
    unsigned speed)
{
    unsigned int changes;
    unsigned int value;

    value = ehci_port_read(sc, port);
    if (companion) {
        if ((value & EHCI_PS_PO) != 0)
            return;
        ehci_port_modify(sc, port, EHCI_PS_PO, 0);
    } else {
        if ((value & EHCI_PS_PO) == 0)
            return;
        ehci_port_modify(sc, port, 0, EHCI_PS_PO);
    }
    value = ehci_port_read(sc, port);
    changes = value & EHCI_PS_CLEAR;
    if (changes != 0)
        ehci_op_write(sc, EHCI_PORTSC(port),
            (value & ~EHCI_PS_CLEAR) | changes);
    sc->eh_reset_change &= ~(1u << (port - 1u));
    if (sc->eh_owner_change != 0)
        sc->eh_owner_change(sc->eh_owner_arg, port, companion, speed);
}

usb_error_t
ehci_root_port_status(struct ehci_softc *sc, unsigned port,
    usb_port_status_t *status)
{
    unsigned int value;
    unsigned int port_status;
    unsigned int port_change;

    if (sc == 0 || status == 0 || !sc->eh_started || port == 0 ||
        port > sc->eh_nports)
        return USB_STATUS_INVALID;
    value = ehci_port_read(sc, port);
    port_status = 0;
    port_change = 0;
    if (value & EHCI_PS_PP)
        port_status |= UPS_PORT_POWER;
    if (value & EHCI_PS_CSC)
        port_change |= UPS_C_CONNECT_STATUS;
    if (value & EHCI_PS_PEC)
        port_change |= UPS_C_PORT_ENABLED;
    if (value & EHCI_PS_OCC)
        port_change |= UPS_C_OVERCURRENT_INDICATOR;
    if ((value & EHCI_PS_PO) == 0) {
        port_status |= UPS_HIGH_SPEED;
        if (value & EHCI_PS_CS)
            port_status |= UPS_CURRENT_CONNECT_STATUS;
        if (value & EHCI_PS_PE)
            port_status |= UPS_PORT_ENABLED;
        if (value & EHCI_PS_SUSP)
            port_status |= UPS_SUSPEND;
        if (value & EHCI_PS_OCA)
            port_status |= UPS_OVERCURRENT_INDICATOR;
        if (value & EHCI_PS_PR)
            port_status |= UPS_RESET;
        if (sc->eh_reset_change & (1u << (port - 1u)))
            port_change |= UPS_C_PORT_RESET;
    }
    USETW(status->wPortStatus, port_status);
    USETW(status->wPortChange, port_change);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ehci_root_port_power(struct ehci_softc *sc, unsigned port, int on)
{
    if (sc == 0 || !sc->eh_started || port == 0 || port > sc->eh_nports)
        return USB_STATUS_INVALID;
    if (sc->eh_ppc) {
        ehci_port_modify(sc, port, on ? EHCI_PS_PP : 0,
            on ? 0 : EHCI_PS_PP);
        if (on)
            ehci_delay(sc, 20);
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ehci_root_port_reset(struct ehci_softc *sc, unsigned port)
{
    unsigned int value;

    if (sc == 0 || !sc->eh_started || port == 0 || port > sc->eh_nports)
        return USB_STATUS_INVALID;
    value = ehci_port_read(sc, port);
    if ((value & EHCI_PS_CS) == 0 || (value & EHCI_PS_PO) != 0)
        return USB_STATUS_DISCONNECTED;
    if (EHCI_PS_IS_LOWSPEED(value)) {
        ehci_set_port_owner(sc, port, 1, USB_SPEED_LOW);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    ehci_port_modify(sc, port, EHCI_PS_PR, EHCI_PS_PE);
    ehci_delay(sc, USB_PORT_ROOT_RESET_DELAY);
    ehci_port_modify(sc, port, 0, EHCI_PS_PR | EHCI_PS_PE);
    ehci_delay(sc, EHCI_PORT_RESET_COMPLETE_MS);
    value = ehci_port_read(sc, port);
    if (value & EHCI_PS_PR)
        return USB_STATUS_TIMEOUT;
    if ((value & EHCI_PS_CS) == 0)
        return USB_STATUS_DISCONNECTED;
    if ((value & EHCI_PS_PE) == 0) {
        ehci_set_port_owner(sc, port, 1, USB_SPEED_FULL);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    /* Device recovery follows reset signalling before the first SETUP. */
    ehci_delay(sc, USB_PORT_RESET_RECOVERY);
    sc->eh_reset_change |= 1u << (port - 1u);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ehci_root_port_clear_change(struct ehci_softc *sc, unsigned port,
    unsigned change)
{
    unsigned int value;

    if (sc == 0 || !sc->eh_started || port == 0 || port > sc->eh_nports)
        return USB_STATUS_INVALID;
    value = 0;
    if (change & UPS_C_CONNECT_STATUS)
        value |= EHCI_PS_CSC;
    if (change & UPS_C_PORT_ENABLED)
        value |= EHCI_PS_PEC;
    if (change & UPS_C_OVERCURRENT_INDICATOR)
        value |= EHCI_PS_OCC;
    if (value != 0)
        ehci_op_write(sc, EHCI_PORTSC(port),
            (ehci_port_read(sc, port) & ~EHCI_PS_CLEAR) | value);
    if (change & UPS_C_PORT_RESET)
        sc->eh_reset_change &= ~(1u << (port - 1u));
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
ehci_reclaim_port(struct ehci_softc *sc, unsigned port)
{
    unsigned int value;

    if (sc == 0 || !sc->eh_started || port == 0 || port > sc->eh_nports)
        return USB_STATUS_INVALID;
    value = ehci_port_read(sc, port);
    if ((value & EHCI_PS_PO) == 0)
        return USB_STATUS_NORMAL_COMPLETION;
    ehci_set_port_owner(sc, port, 0, USB_SPEED_UNKNOWN);
    ehci_delay(sc, 1);
    value = ehci_port_read(sc, port);
    if ((value & EHCI_PS_CS) != 0 && sc->eh_hcd.uh_root_change != 0)
        (void)sc->eh_hcd.uh_root_change(sc->eh_hcd.uh_root_change_arg);
    return USB_STATUS_NORMAL_COMPLETION;
}

void
ehci_root_intr_enable(struct ehci_softc *sc, int on)
{
    unsigned int enabled;

    if (sc == 0 || !sc->eh_started)
        return;
    enabled = ehci_op_read(sc, EHCI_USBINTR);
    if (on) {
        enabled |= EHCI_INTR_PCIE;
        sc->eh_root_intr_enabled = 1;
    } else {
        enabled &= ~EHCI_INTR_PCIE;
        sc->eh_root_intr_enabled = 0;
    }
    ehci_op_write(sc, EHCI_USBINTR, enabled);
}

int
ehci_intr(struct ehci_softc *sc)
{
    unsigned int enabled;
    unsigned int status;

    if (sc == 0 || !sc->eh_started)
        return 0;
    enabled = ehci_op_read(sc, EHCI_USBINTR);
    status = ehci_op_read(sc, EHCI_USBSTS) & EHCI_STS_INTRS;
    status &= enabled | EHCI_STS_HSE;
    if (status == 0)
        return 0;
    ehci_op_write(sc, EHCI_USBSTS, status);
    if ((status & EHCI_STS_HSE) != 0 && sc->eh_active_xfer != 0)
        ehci_complete_active(sc, USB_STATUS_IO_ERROR, 0);
    else if (status & (EHCI_STS_INT | EHCI_STS_ERRINT))
        ehci_hcd_poll(&sc->eh_hcd);
    if (status & EHCI_STS_PCD) {
        ehci_root_intr_enable(sc, 0);
        if (sc->eh_hcd.uh_root_change == 0 ||
            sc->eh_hcd.uh_root_change(sc->eh_hcd.uh_root_change_arg) != 0)
            ehci_root_intr_enable(sc, 1);
    }
    return 1;
}

static unsigned
ehci_hcd_root_port_count(struct usb_hcd *hcd)
{
    struct ehci_softc *sc;

    sc = (struct ehci_softc *)hcd->uh_softc;
    return sc != 0 && sc->eh_started ? sc->eh_nports : 0;
}

static usb_error_t
ehci_hcd_root_port_status(struct usb_hcd *hcd, unsigned port,
    usb_port_status_t *status)
{
    return ehci_root_port_status((struct ehci_softc *)hcd->uh_softc,
        port, status);
}

static usb_error_t
ehci_hcd_root_port_power(struct usb_hcd *hcd, unsigned port, int on)
{
    return ehci_root_port_power((struct ehci_softc *)hcd->uh_softc,
        port, on);
}

static usb_error_t
ehci_hcd_root_port_reset(struct usb_hcd *hcd, unsigned port)
{
    return ehci_root_port_reset((struct ehci_softc *)hcd->uh_softc, port);
}

static usb_error_t
ehci_hcd_root_port_clear_change(struct usb_hcd *hcd, unsigned port,
    unsigned change)
{
    return ehci_root_port_clear_change(
        (struct ehci_softc *)hcd->uh_softc, port, change);
}

static void
ehci_hcd_root_intr_enable(struct usb_hcd *hcd, int on)
{
    ehci_root_intr_enable((struct ehci_softc *)hcd->uh_softc, on);
}
