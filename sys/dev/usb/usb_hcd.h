/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DEV_USB_USB_HCD_H_
#define _DEV_USB_USB_HCD_H_

#include <stddef.h>
#include <dev/usb/usbdi.h>

struct usb_hcd;
struct usb_pipe;
struct usb_xfer;

struct usb_hcd_ops {
    usb_error_t (*uho_start)(struct usb_hcd *);
    void (*uho_stop)(struct usb_hcd *);
    usb_error_t (*uho_open_pipe)(struct usb_pipe *);
    void (*uho_close_pipe)(struct usb_pipe *);
    usb_error_t (*uho_submit_xfer)(struct usb_xfer *);
    usb_error_t (*uho_abort_xfer)(struct usb_xfer *);
    usb_error_t (*uho_root_ctrl)(struct usb_hcd *,
        const usb_device_request_t *, void *, size_t *);
    void (*uho_poll)(struct usb_hcd *);
};

struct usb_hcd {
    const struct usb_hcd_ops *uh_ops;
    struct usb_bus *uh_bus;
    void *uh_softc;
    unsigned uh_running;
};

#endif /* _DEV_USB_USB_HCD_H_ */
