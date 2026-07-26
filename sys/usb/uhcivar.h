/*	$NetBSD: uhcivar.h,v 1.57 2020/03/15 07:56:19 skrll Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

#ifndef _USB_UHCIVAR_H_
#define _USB_UHCIVAR_H_

#include <sys/dma.h>
#include <usb/uhcireg.h>
#include <usb/usbvar.h>

#define UHCI_SCHEDULE_BYTES          (24u * 1024u)
#define UHCI_FRAME_LIST_OFFSET       0u
#define UHCI_INTR_QH_OFFSET          4096u
#define UHCI_CONTROL_QH_OFFSET       4112u
#define UHCI_BULK_QH_OFFSET          4128u
#define UHCI_LAST_QH_OFFSET          4144u
#define UHCI_TD_OFFSET               4352u
#define UHCI_TD_COUNT                130u
#define UHCI_INTR_TD_OFFSET          6432u
#define UHCI_DUMMY_TD_OFFSET         6448u
#define UHCI_SETUP_OFFSET            6464u
#define UHCI_INTR_BUFFER_OFFSET      6528u
#define UHCI_DATA_OFFSET             8192u
#define UHCI_CONTROL_DATA_MAX        1024u
#define UHCI_INTR_DATA_MAX           64u
#define UHCI_DATA_CHUNK_MAX          \
    (UHCI_SCHEDULE_BYTES - UHCI_DATA_OFFSET)
#define UHCI_BULK_DATA_MAX           (128u * 1024u)
#define UHCI_ROOT_PORTS              2u

typedef unsigned short (*uhci_read_2_t)(void *, unsigned);
typedef void (*uhci_write_2_t)(void *, unsigned, unsigned short);
typedef unsigned int (*uhci_read_4_t)(void *, unsigned);
typedef void (*uhci_write_4_t)(void *, unsigned, unsigned int);
typedef void (*uhci_delay_ms_t)(void *, unsigned);

struct uhci_pipe {
    struct usb_pipe *up_pipe;
    unsigned up_used;
    unsigned up_toggle;
};

struct uhci_softc {
    struct usb_hcd uh_hcd;
    uhci_read_2_t uh_read_2;
    uhci_write_2_t uh_write_2;
    uhci_read_4_t uh_read_4;
    uhci_write_4_t uh_write_4;
    uhci_delay_ms_t uh_delay_ms;
    void *uh_io_arg;
    struct dma_mem uh_schedule_dma;
    unsigned int *uh_frame_list;
    struct uhci_qh *uh_intr_qh;
    struct uhci_qh *uh_control_qh;
    struct uhci_qh *uh_bulk_qh;
    struct uhci_qh *uh_last_qh;
    struct uhci_td *uh_tds;
    struct uhci_td *uh_intr_td;
    struct uhci_td *uh_dummy_td;
    uByte *uh_setup_buffer;
    uByte *uh_intr_buffer;
    uByte *uh_data_buffer;
    struct uhci_pipe uh_pipes[USB_MAX_PIPES];
    struct usb_xfer *uh_active_xfer;
    struct uhci_pipe *uh_active_pipe;
    unsigned uh_active_td_count;
    unsigned uh_active_status_td;
    unsigned uh_active_offset;
    unsigned uh_active_chunk;
    unsigned uh_active_data_in;
    unsigned uh_active_control;
    unsigned uh_active_control_short;
    struct usb_xfer *uh_intr_xfer;
    struct uhci_pipe *uh_intr_pipe;
    unsigned uh_intr_length;
    unsigned uh_intr_interval;
    unsigned uh_reset_change;
    unsigned uh_root_intr_enabled;
    unsigned uh_root_change_pending;
    unsigned uh_started;
#ifdef KERNEL
    unsigned uh_watchdog_armed;
#endif
};

void uhci_softc_init(struct uhci_softc *, uhci_read_2_t, uhci_write_2_t,
    uhci_read_4_t, uhci_write_4_t, uhci_delay_ms_t, void *);
usb_error_t uhci_root_port_status(struct uhci_softc *, unsigned,
    usb_port_status_t *);
usb_error_t uhci_root_port_power(struct uhci_softc *, unsigned, int);
usb_error_t uhci_root_port_reset(struct uhci_softc *, unsigned);
usb_error_t uhci_root_port_clear_change(struct uhci_softc *, unsigned,
    unsigned);
void uhci_root_intr_enable(struct uhci_softc *, int);
int uhci_intr(struct uhci_softc *);

#endif /* _USB_UHCIVAR_H_ */
