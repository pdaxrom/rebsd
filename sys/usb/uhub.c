/*	$NetBSD: uhub.c,v 1.74 2005/03/02 11:37:27 mycroft Exp $	*/

/*
 * Copyright (c) 1998, 2004 The NetBSD Foundation, Inc.
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
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <usb/uhub.h>

#define USB_ROOT_HUB_DEBOUNCE_MS    100u
#define USB_ROOT_HUB_ALL_CHANGES    (UPS_C_CONNECT_STATUS | \
    UPS_C_PORT_ENABLED | UPS_C_SUSPEND | \
    UPS_C_OVERCURRENT_INDICATOR | UPS_C_PORT_RESET)

void
uhubattach(int unit)
{
    (void)unit;
}

static void
usb_root_hub_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
usb_root_hub_event(struct usb_root_hub *hub, unsigned port,
    enum usb_root_hub_event event, struct usb_device *device,
    usb_error_t status)
{
    hub->urh_last_error = status;
    if (hub->urh_event != 0)
        hub->urh_event(hub->urh_event_arg, port, event, device, status);
}

static usb_error_t
usb_root_hub_clear_change(struct usb_root_hub *hub, unsigned port,
    unsigned change)
{
    if (change == 0)
        return USB_STATUS_NORMAL_COMPLETION;
    return hub->urh_bus->ub_hcd->uh_ops->uho_root_port_clear_change(
        hub->urh_bus->ub_hcd, port,
        change & USB_ROOT_HUB_ALL_CHANGES);
}

static usb_error_t
usb_root_hub_get_status(struct usb_root_hub *hub, unsigned port,
    usb_port_status_t *port_status)
{
    usb_error_t status;

    status = hub->urh_bus->ub_hcd->uh_ops->uho_root_port_status(
        hub->urh_bus->ub_hcd, port, port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_STATUS_ERROR,
            0, status);
    return status;
}

static void
usb_root_hub_explore_port(struct usb_root_hub *hub, unsigned port)
{
    struct usb_root_hub_port *hub_port;
    struct usb_device *device;
    usb_port_status_t port_status;
    usb_error_t status;
    unsigned change;
    unsigned flags;
    unsigned speed;

    hub_port = &hub->urh_ports[port - 1];
    status = usb_root_hub_get_status(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return;
    flags = UGETW(port_status.wPortStatus);
    change = UGETW(port_status.wPortChange);
    status = usb_root_hub_clear_change(hub, port, change);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_STATUS_ERROR,
            0, status);
        return;
    }

    if (hub_port->urp_device != 0 &&
        ((change & (UPS_C_CONNECT_STATUS | UPS_C_PORT_ENABLED)) != 0 ||
        (flags & UPS_CURRENT_CONNECT_STATUS) == 0)) {
        usb_device_disconnect(hub_port->urp_device);
        hub_port->urp_device = 0;
        usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_DETACH, 0,
            USB_STATUS_NORMAL_COMPLETION);
    }
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0 ||
        hub_port->urp_device != 0)
        return;

    if (hub->urh_bus->ub_delay_ms != 0)
        hub->urh_bus->ub_delay_ms(hub->urh_bus->ub_delay_arg,
            USB_ROOT_HUB_DEBOUNCE_MS);
    status = usb_root_hub_get_status(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return;
    flags = UGETW(port_status.wPortStatus);
    change = UGETW(port_status.wPortChange);
    (void)usb_root_hub_clear_change(hub, port, change);
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0)
        return;

    status = hub->urh_bus->ub_hcd->uh_ops->uho_root_port_reset(
        hub->urh_bus->ub_hcd, port);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_RESET_ERROR,
            0, status);
        return;
    }
    status = usb_root_hub_get_status(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return;
    flags = UGETW(port_status.wPortStatus);
    change = UGETW(port_status.wPortChange);
    (void)usb_root_hub_clear_change(hub, port, change);
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0)
        return;
    if (flags & UPS_HIGH_SPEED)
        speed = USB_SPEED_HIGH;
    else if (flags & UPS_LOW_SPEED)
        speed = USB_SPEED_LOW;
    else
        speed = USB_SPEED_FULL;

    device = 0;
    status = usb_device_enumerate(hub->urh_bus, port, speed, &device);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_ENUM_ERROR,
            0, status);
        return;
    }
    hub_port->urp_device = device;
    usb_root_hub_event(hub, port, USB_ROOT_HUB_EVENT_ATTACH, device,
        USB_STATUS_NORMAL_COMPLETION);
}

static void
usb_root_hub_explore(void *arg)
{
    struct usb_root_hub *hub;
    unsigned port;

    hub = arg;
    if (hub == 0 || !hub->urh_started)
        return;
    for (port = 1; port <= hub->urh_port_count; ++port)
        usb_root_hub_explore_port(hub, port);
    hub->urh_bus->ub_hcd->uh_ops->uho_root_intr_enable(
        hub->urh_bus->ub_hcd, 1);
}

static int
usb_root_hub_changed(void *arg)
{
    struct usb_root_hub *hub;

    hub = arg;
    if (hub == 0 || !hub->urh_started)
        return -1;
    return usb_task_schedule(&hub->urh_task);
}

usb_error_t
usb_root_hub_start(struct usb_root_hub *hub, struct usb_bus *bus,
    usb_root_hub_event_t event, void *event_arg)
{
    const struct usb_hcd_ops *ops;
    usb_error_t status;
    unsigned port;
    unsigned ports;

    if (hub == 0 || bus == 0 || !bus->ub_started || bus->ub_hcd == 0)
        return USB_STATUS_INVALID;
    ops = bus->ub_hcd->uh_ops;
    if (ops->uho_root_port_count == 0 ||
        ops->uho_root_port_status == 0 ||
        ops->uho_root_port_power == 0 ||
        ops->uho_root_port_reset == 0 ||
        ops->uho_root_port_clear_change == 0 ||
        ops->uho_root_intr_enable == 0)
        return USB_STATUS_UNSUPPORTED;
    ports = ops->uho_root_port_count(bus->ub_hcd);
    if (ports == 0 || ports > USB_MAX_ROOT_PORTS)
        return USB_STATUS_NO_MEMORY;

    usb_root_hub_zero(hub, sizeof(*hub));
    hub->urh_bus = bus;
    hub->urh_event = event;
    hub->urh_event_arg = event_arg;
    hub->urh_port_count = ports;
    hub->urh_last_error = USB_STATUS_NORMAL_COMPLETION;
    usb_task_init(&hub->urh_task, usb_root_hub_explore, hub);
    bus->ub_hcd->uh_root_change = usb_root_hub_changed;
    bus->ub_hcd->uh_root_change_arg = hub;
    hub->urh_started = 1;

    for (port = 1; port <= ports; ++port) {
        status = ops->uho_root_port_power(bus->ub_hcd, port, 1);
        if (status != USB_STATUS_NORMAL_COMPLETION) {
            usb_root_hub_event(hub, port,
                USB_ROOT_HUB_EVENT_POWER_ERROR, 0, status);
            usb_root_hub_stop(hub);
            return status;
        }
    }
    usb_root_hub_explore(hub);
    return USB_STATUS_NORMAL_COMPLETION;
}

void
usb_root_hub_stop(struct usb_root_hub *hub)
{
    const struct usb_hcd_ops *ops;
    unsigned port;

    if (hub == 0 || !hub->urh_started)
        return;
    ops = hub->urh_bus->ub_hcd->uh_ops;
    hub->urh_started = 0;
    ops->uho_root_intr_enable(hub->urh_bus->ub_hcd, 0);
    hub->urh_bus->ub_hcd->uh_root_change = 0;
    hub->urh_bus->ub_hcd->uh_root_change_arg = 0;
    usb_task_cancel(&hub->urh_task);
    for (port = 1; port <= hub->urh_port_count; ++port) {
        if (hub->urh_ports[port - 1].urp_device != 0) {
            usb_device_disconnect(
                hub->urh_ports[port - 1].urp_device);
            hub->urh_ports[port - 1].urp_device = 0;
            usb_root_hub_event(hub, port,
                USB_ROOT_HUB_EVENT_DETACH, 0,
                USB_STATUS_NORMAL_COMPLETION);
        }
        (void)ops->uho_root_port_power(hub->urh_bus->ub_hcd,
            port, 0);
    }
}

struct usb_device *
usb_root_hub_device(struct usb_root_hub *hub, unsigned port)
{
    if (hub == 0 || !hub->urh_started || port == 0 ||
        port > hub->urh_port_count)
        return 0;
    return hub->urh_ports[port - 1].urp_device;
}
