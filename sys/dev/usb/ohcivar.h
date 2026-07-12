/*	$NetBSD: ohcivar.h,v 1.36 2005/03/11 19:25:22 mycroft Exp $	*/

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

#ifndef _DEV_USB_OHCIVAR_H_
#define _DEV_USB_OHCIVAR_H_

#include <sys/dma.h>
#include <dev/usb/ohcireg.h>
#include <dev/usb/usbvar.h>

#define OHCI_SCHEDULE_BYTES         4096u
#define OHCI_HCCA_OFFSET            0u
#define OHCI_ED_OFFSET              256u
#define OHCI_TD_OFFSET              512u
#define OHCI_SETUP_OFFSET           1024u
#define OHCI_DATA_OFFSET            1280u
#define OHCI_CONTROL_DATA_MAX       \
    (OHCI_SCHEDULE_BYTES - OHCI_DATA_OFFSET)
#define OHCI_TD_COUNT               4u

typedef unsigned int (*ohci_read_4_t)(void *, unsigned);
typedef void (*ohci_write_4_t)(void *, unsigned, unsigned int);
typedef void (*ohci_delay_ms_t)(void *, unsigned);

struct ohci_pipe {
    struct usb_pipe *op_pipe;
    unsigned op_used;
};

struct ohci_softc {
    struct usb_hcd oh_hcd;
    ohci_read_4_t oh_read_4;
    ohci_write_4_t oh_write_4;
    ohci_delay_ms_t oh_delay_ms;
    void *oh_io_arg;
    struct dma_mem oh_schedule_dma;
    struct ohci_hcca *oh_hcca;
    struct ohci_ed *oh_control_ed;
    struct ohci_td *oh_tds;
    uByte *oh_setup_buffer;
    uByte *oh_data_buffer;
    struct ohci_pipe oh_pipes[USB_MAX_PIPES];
    struct usb_xfer *oh_active_xfer;
    unsigned oh_active_data_length;
    unsigned oh_active_data_in;
    unsigned oh_nports;
    unsigned oh_revision;
    unsigned oh_started;
};

void ohci_softc_init(struct ohci_softc *, ohci_read_4_t, ohci_write_4_t,
    ohci_delay_ms_t, void *);
usb_error_t ohci_root_port_status(struct ohci_softc *, unsigned,
    usb_port_status_t *);
usb_error_t ohci_root_port_power(struct ohci_softc *, unsigned, int);
usb_error_t ohci_root_port_reset(struct ohci_softc *, unsigned);

#endif /* _DEV_USB_OHCIVAR_H_ */
