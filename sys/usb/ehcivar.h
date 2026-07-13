/*	$NetBSD: ehcivar.h,v 1.23.2.1 2005/09/26 15:20:49 tron Exp $	*/

/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
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

#ifndef _USB_EHCIVAR_H_
#define _USB_EHCIVAR_H_

#include <sys/dma.h>
#include <usb/ehcireg.h>
#include <usb/usbvar.h>

#define EHCI_SCHEDULE_BYTES         16384u
#define EHCI_FRAME_LIST_OFFSET      0u
#define EHCI_FRAME_LIST_COUNT       1024u
#define EHCI_ASYNC_HEAD_OFFSET      4096u
#define EHCI_PIPE_QH_OFFSET         4192u
#define EHCI_QTD_OFFSET             6144u
#define EHCI_QTD_COUNT              4u
#define EHCI_SETUP_OFFSET           6400u
#define EHCI_DATA_OFFSET            8192u
#define EHCI_DATA_MAX               (EHCI_SCHEDULE_BYTES - EHCI_DATA_OFFSET)

typedef unsigned int (*ehci_read_4_t)(void *, unsigned);
typedef void (*ehci_write_4_t)(void *, unsigned, unsigned int);
typedef void (*ehci_delay_ms_t)(void *, unsigned);
typedef void (*ehci_owner_change_t)(void *, unsigned, int, unsigned);

struct ehci_pipe {
    struct usb_pipe *ep_pipe;
    struct ehci_qh *ep_qh;
    unsigned ep_used;
    unsigned ep_toggle;
};

struct ehci_softc {
    struct usb_hcd eh_hcd;
    ehci_read_4_t eh_read_4;
    ehci_write_4_t eh_write_4;
    ehci_delay_ms_t eh_delay_ms;
    void *eh_io_arg;
    ehci_owner_change_t eh_owner_change;
    void *eh_owner_arg;
    struct dma_mem eh_schedule_dma;
    unsigned int *eh_frame_list;
    struct ehci_qh *eh_async_head;
    struct ehci_qh *eh_pipe_qhs;
    struct ehci_qtd *eh_qtds;
    uByte *eh_setup_buffer;
    uByte *eh_data_buffer;
    struct ehci_pipe eh_pipes[USB_MAX_PIPES];
    struct usb_xfer *eh_active_xfer;
    struct ehci_pipe *eh_active_pipe;
    unsigned eh_active_qtds;
    unsigned eh_active_length;
    unsigned eh_active_data_in;
    unsigned eh_active_control;
    unsigned eh_last_qh_phys;
    unsigned eh_last_qtd_phys;
    unsigned eh_last_qh_link;
    unsigned eh_last_qh_endp;
    unsigned eh_last_qh_endphub;
    unsigned eh_last_qh_curqtd;
    unsigned eh_last_qh_next;
    unsigned eh_last_qh_altnext;
    unsigned eh_last_qh_status;
    unsigned eh_last_qtd_next[3];
    unsigned eh_last_qtd_altnext[3];
    unsigned eh_last_qtd_status[3];
    unsigned eh_last_qtd_buffer[3];
    uByte eh_last_setup[8];
    usb_device_request_t eh_last_request;
    unsigned eh_last_address;
    unsigned eh_op_offset;
    unsigned eh_nports;
    unsigned eh_ncomp;
    unsigned eh_npcomp;
    unsigned eh_ppc;
    unsigned eh_revision;
    unsigned eh_reset_change;
    unsigned eh_root_intr_enabled;
    unsigned eh_started;
};

void ehci_softc_init(struct ehci_softc *, ehci_read_4_t, ehci_write_4_t,
    ehci_delay_ms_t, void *);
void ehci_set_owner_callback(struct ehci_softc *, ehci_owner_change_t,
    void *);
usb_error_t ehci_root_port_status(struct ehci_softc *, unsigned,
    usb_port_status_t *);
usb_error_t ehci_root_port_power(struct ehci_softc *, unsigned, int);
usb_error_t ehci_root_port_reset(struct ehci_softc *, unsigned);
usb_error_t ehci_root_port_clear_change(struct ehci_softc *, unsigned,
    unsigned);
usb_error_t ehci_reclaim_port(struct ehci_softc *, unsigned);
void ehci_root_intr_enable(struct ehci_softc *, int);
int ehci_intr(struct ehci_softc *);

#endif /* _USB_EHCIVAR_H_ */
