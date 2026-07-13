/*	$NetBSD: usbdivar.h,v 1.73.6.1 2006/08/11 04:22:21 riz Exp $	*/

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

#ifndef _DEV_USB_USBVAR_H_
#define _DEV_USB_USBVAR_H_

#include <dev/usb/usb_desc.h>
#include <dev/usb/usb_hcd.h>
#include <dev/usb/usb_limits.h>

struct usb_core;

struct usb_endpoint {
    unsigned ue_used;
    struct usb_interface *ue_interface;
    usb_endpoint_descriptor_t ue_desc;
    unsigned ue_refcnt;
};

struct usb_driver {
    const char *ud_name;
    int (*ud_match)(struct usb_interface *);
    usb_error_t (*ud_attach)(struct usb_interface *);
    void (*ud_detach)(struct usb_interface *);
};

struct usb_interface {
    unsigned ui_used;
    struct usb_device *ui_device;
    usb_interface_descriptor_t ui_desc;
    struct usb_endpoint *ui_endpoints[USB_MAX_ENDPOINTS_PER_INTERFACE];
    unsigned ui_endpoint_count;
    const struct usb_driver *ui_driver;
    void *ui_private;
};

struct usb_pipe {
    unsigned up_used;
    unsigned up_running;
    struct usb_device *up_device;
    struct usb_interface *up_interface;
    struct usb_endpoint *up_endpoint;
};

struct usb_xfer {
    unsigned ux_used;
    unsigned ux_active;
    unsigned ux_done;
    struct usb_device *ux_device;
    struct usb_pipe *ux_pipe;
    void *ux_private;
    void *ux_buffer;
    size_t ux_length;
    size_t ux_actlen;
    unsigned ux_flags;
    unsigned ux_timeout_ms;
    usb_error_t ux_status;
    usb_callback_t ux_callback;
    unsigned ux_is_control;
    usb_device_request_t ux_request;
    void *ux_hcpriv;
};

struct usb_device {
    unsigned ud_used;
    unsigned ud_connected;
    struct usb_bus *ud_bus;
    uByte ud_address;
    uByte ud_port;
    uByte ud_speed;
    uByte ud_config;
    usb_device_descriptor_t ud_desc;
    usb_endpoint_descriptor_t ud_default_desc;
    struct usb_endpoint ud_default_endpoint;
    struct usb_pipe *ud_default_pipe;
    struct usb_interface *ud_interfaces[USB_MAX_INTERFACES];
    unsigned ud_interface_count;
    size_t ud_config_length;
    uByte ud_config_data[USB_MAX_CONFIG_DESCRIPTOR_SIZE];
};

typedef void (*usb_delay_ms_t)(void *, unsigned);

struct usb_bus {
    struct usb_core *ub_core;
    struct usb_hcd *ub_hcd;
    usb_delay_ms_t ub_delay_ms;
    void *ub_delay_arg;
    uByte ub_addresses[16];
    unsigned ub_started;
};

struct usb_core {
    struct usb_device uc_devices[USB_MAX_DEVICES];
    struct usb_interface uc_interfaces[USB_MAX_CORE_INTERFACES];
    struct usb_endpoint uc_endpoints[USB_MAX_CORE_ENDPOINTS];
    struct usb_pipe uc_pipes[USB_MAX_PIPES];
    struct usb_xfer uc_xfers[USB_MAX_XFERS];
    const struct usb_driver *uc_drivers[USB_MAX_DRIVERS];
    unsigned uc_driver_count;
    unsigned uc_enumerating;
    struct usb_parsed_config uc_parsed_config;
};

void usb_core_init(struct usb_core *);
struct usb_core *usb_core_default(void);
usb_error_t usb_driver_register(struct usb_core *,
    const struct usb_driver *);
usb_error_t usb_bus_start(struct usb_core *, struct usb_bus *,
    struct usb_hcd *, usb_delay_ms_t, void *);
void usb_bus_stop(struct usb_bus *);

usb_error_t usb_device_enumerate(struct usb_bus *, unsigned, unsigned,
    struct usb_device **);
void usb_device_disconnect(struct usb_device *);

struct usb_interface *usb_device_interface(struct usb_device *, unsigned);
struct usb_endpoint *usb_interface_endpoint(struct usb_interface *, unsigned);

#endif /* _DEV_USB_USBVAR_H_ */
