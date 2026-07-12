/*	$NetBSD: usbdi.h,v 1.64 2004/10/23 13:26:34 augustss Exp $	*/

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

#ifndef _DEV_USB_USBDI_H_
#define _DEV_USB_USBDI_H_

#include <stddef.h>
#include <dev/usb/usb.h>

struct usb_bus;
struct usb_device;
struct usb_interface;
struct usb_pipe;
struct usb_xfer;

typedef enum usb_status {
    USB_STATUS_NORMAL_COMPLETION = 0,
    USB_STATUS_IN_PROGRESS,
    USB_STATUS_INVALID,
    USB_STATUS_NO_MEMORY,
    USB_STATUS_CANCELLED,
    USB_STATUS_NO_ADDRESS,
    USB_STATUS_IO_ERROR,
    USB_STATUS_NOT_CONFIGURED,
    USB_STATUS_TIMEOUT,
    USB_STATUS_SHORT_XFER,
    USB_STATUS_STALLED,
    USB_STATUS_DISCONNECTED,
    USB_STATUS_INVALID_DESCRIPTOR,
    USB_STATUS_UNSUPPORTED
} usb_error_t;

typedef void (*usb_callback_t)(struct usb_xfer *, void *, usb_error_t);

#define USB_XFER_SHORT_OK       0x0001u
#define USB_XFER_SYNCHRONOUS    0x0002u
#define USB_DEFAULT_TIMEOUT_MS  5000u
#define USB_ENUM_TIMEOUT_MS     250u

const char *usb_status_string(usb_error_t);

usb_error_t usb_open_pipe(struct usb_interface *, uByte,
    struct usb_pipe **);
void usb_close_pipe(struct usb_pipe *);

struct usb_xfer *usb_alloc_xfer(struct usb_device *);
usb_error_t usb_free_xfer(struct usb_xfer *);
void usb_setup_xfer(struct usb_xfer *, struct usb_pipe *, void *, void *,
    size_t, unsigned, unsigned, usb_callback_t);
usb_error_t usb_submit_xfer(struct usb_xfer *);
usb_error_t usb_abort_xfer(struct usb_xfer *, usb_error_t);
void usb_xfer_complete(struct usb_xfer *, usb_error_t, size_t);

usb_error_t usb_control_request(struct usb_device *,
    const usb_device_request_t *, void *, size_t, unsigned, size_t *);

#endif /* _DEV_USB_USBDI_H_ */
