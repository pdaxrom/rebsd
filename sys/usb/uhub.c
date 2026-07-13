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

#ifdef KERNEL
#include <sys/types.h>
#include <sys/systm.h>
#else
#include <stdio.h>
#endif

#define USB_ROOT_HUB_DEBOUNCE_MS    100u
#define USB_ROOT_HUB_ALL_CHANGES    (UPS_C_CONNECT_STATUS | \
    UPS_C_PORT_ENABLED | UPS_C_SUSPEND | \
    UPS_C_OVERCURRENT_INDICATOR | UPS_C_PORT_RESET)

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

/*
 * External hub class driver.  This follows the compact attach/explore flow
 * from NetBSD 3.1 uhub(4), using ReBSD's fixed pools and USB task queue.
 */

#define UHUB_DEBOUNCE_MS             100u
#define UHUB_POWER_EXTRA_MS          20u
#define UHUB_RESET_RETRIES           10u

static struct usb_external_hub usb_external_hubs[USB_MAX_HUBS];

static void
uhub_make_request(usb_device_request_t *request, uByte type, uByte code,
    unsigned value, unsigned index, unsigned length)
{
    usb_root_hub_zero(request, sizeof(*request));
    request->bmRequestType = type;
    request->bRequest = code;
    USETW(request->wValue, value);
    USETW(request->wIndex, index);
    USETW(request->wLength, length);
}

static usb_error_t
uhub_request(struct usb_external_hub *hub, uByte type, uByte code,
    unsigned value, unsigned index, void *buffer, unsigned length)
{
    usb_device_request_t request;
    size_t actlen;
    usb_error_t status;

    uhub_make_request(&request, type, code, value, index, length);
    status = usb_control_request(hub->ueh_device, &request, buffer,
        length, USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return status;
    if (length != 0 && actlen < length)
        return USB_STATUS_SHORT_XFER;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
uhub_port_feature(struct usb_external_hub *hub, unsigned port,
    unsigned feature, int set)
{
    return uhub_request(hub, UT_WRITE_CLASS_OTHER,
        set ? UR_SET_FEATURE : UR_CLEAR_FEATURE, feature, port, 0, 0);
}

static usb_error_t
uhub_port_status(struct usb_external_hub *hub, unsigned port,
    usb_port_status_t *status)
{
    return uhub_request(hub, UT_READ_CLASS_OTHER, UR_GET_STATUS, 0,
        port, status, sizeof(*status));
}

static void
uhub_clear_changes(struct usb_external_hub *hub, unsigned port,
    unsigned change)
{
    if (change & UPS_C_CONNECT_STATUS)
        (void)uhub_port_feature(hub, port, UHF_C_PORT_CONNECTION, 0);
    if (change & UPS_C_PORT_ENABLED)
        (void)uhub_port_feature(hub, port, UHF_C_PORT_ENABLE, 0);
    if (change & UPS_C_SUSPEND)
        (void)uhub_port_feature(hub, port, UHF_C_PORT_SUSPEND, 0);
    if (change & UPS_C_OVERCURRENT_INDICATOR)
        (void)uhub_port_feature(hub, port,
            UHF_C_PORT_OVER_CURRENT, 0);
    if (change & UPS_C_PORT_RESET)
        (void)uhub_port_feature(hub, port, UHF_C_PORT_RESET, 0);
}

static usb_error_t
uhub_reset_port(struct usb_external_hub *hub, unsigned port,
    usb_port_status_t *port_status)
{
    usb_error_t status;
    unsigned flags;
    unsigned change;
    unsigned retry;

    status = uhub_port_feature(hub, port, UHF_PORT_RESET, 1);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return status;
    for (retry = 0; retry < UHUB_RESET_RETRIES; ++retry) {
        if (hub->ueh_device->ud_bus->ub_delay_ms != 0)
            hub->ueh_device->ud_bus->ub_delay_ms(
                hub->ueh_device->ud_bus->ub_delay_arg,
                USB_PORT_RESET_DELAY);
        status = uhub_port_status(hub, port, port_status);
        if (status != USB_STATUS_NORMAL_COMPLETION)
            return status;
        flags = UGETW(port_status->wPortStatus);
        change = UGETW(port_status->wPortChange);
        if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0)
            return USB_STATUS_DISCONNECTED;
        if (change & UPS_C_PORT_RESET)
            break;
    }
    if (retry == UHUB_RESET_RETRIES)
        return USB_STATUS_TIMEOUT;
    status = uhub_port_feature(hub, port, UHF_C_PORT_RESET, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return status;
    if (hub->ueh_device->ud_bus->ub_delay_ms != 0)
        hub->ueh_device->ud_bus->ub_delay_ms(
            hub->ueh_device->ud_bus->ub_delay_arg,
            USB_PORT_RESET_RECOVERY);
    return uhub_port_status(hub, port, port_status);
}

static const char *
uhub_speed_name(unsigned speed)
{
    if (speed == USB_SPEED_HIGH)
        return "high";
    if (speed == USB_SPEED_LOW)
        return "low";
    return "full";
}

static void
uhub_explore_port(struct usb_external_hub *hub, unsigned port)
{
    struct usb_external_hub_port *hub_port;
    struct usb_device *device;
    usb_port_status_t port_status;
    usb_error_t status;
    unsigned flags;
    unsigned change;
    unsigned speed;

    hub_port = &hub->ueh_ports[port - 1u];
    status = uhub_port_status(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("uhub%u: port%u status failed: %s\n", hub->ueh_unit,
            port, usb_status_string(status));
        return;
    }
    flags = UGETW(port_status.wPortStatus);
    change = UGETW(port_status.wPortChange);
    if (change & UPS_C_OVERCURRENT_INDICATOR)
        printf("uhub%u: port%u over-current change\n",
            hub->ueh_unit, port);

    if (hub_port->uep_device != 0 &&
        ((flags & UPS_CURRENT_CONNECT_STATUS) == 0 ||
        (flags & UPS_PORT_ENABLED) == 0 ||
        (change & (UPS_C_CONNECT_STATUS | UPS_C_PORT_ENABLED)) != 0)) {
        usb_device_disconnect(hub_port->uep_device);
        hub_port->uep_device = 0;
        printf("uhub%u: port%u device disconnected\n",
            hub->ueh_unit, port);
    }
    uhub_clear_changes(hub, port, change);
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0 ||
        hub_port->uep_device != 0)
        return;

    if (hub->ueh_device->ud_bus->ub_delay_ms != 0)
        hub->ueh_device->ud_bus->ub_delay_ms(
            hub->ueh_device->ud_bus->ub_delay_arg, UHUB_DEBOUNCE_MS);
    status = uhub_port_status(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return;
    flags = UGETW(port_status.wPortStatus);
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0)
        return;
    status = uhub_reset_port(hub, port, &port_status);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("uhub%u: port%u reset failed: %s\n", hub->ueh_unit,
            port, usb_status_string(status));
        return;
    }
    flags = UGETW(port_status.wPortStatus);
    if ((flags & UPS_CURRENT_CONNECT_STATUS) == 0 ||
        (flags & UPS_PORT_ENABLED) == 0)
        return;
    speed = (flags & UPS_HIGH_SPEED) != 0 ? USB_SPEED_HIGH :
        (flags & UPS_LOW_SPEED) != 0 ? USB_SPEED_LOW : USB_SPEED_FULL;
    device = 0;
    status = usb_device_enumerate_at(hub->ueh_device->ud_bus,
        hub->ueh_device, port, speed, &device);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        printf("uhub%u: port%u enumeration failed: %s\n",
            hub->ueh_unit, port, usb_status_string(status));
        (void)uhub_port_feature(hub, port, UHF_PORT_ENABLE, 0);
        return;
    }
    hub_port->uep_device = device;
    printf("uhub%u: port%u device attached speed=%s addr=%u "
        "vendor=%x product=%x\n", hub->ueh_unit, port,
        uhub_speed_name(speed), device->ud_address,
        UGETW(device->ud_desc.idVendor), UGETW(device->ud_desc.idProduct));
}

static void
uhub_explore(void *arg)
{
    struct usb_external_hub *hub;
    unsigned port;

    hub = (struct usb_external_hub *)arg;
    if (hub == 0 || !hub->ueh_used || hub->ueh_dying)
        return;
    if (hub->ueh_clear_stall) {
        hub->ueh_clear_stall = 0;
        (void)usb_clear_endpoint_halt(hub->ueh_intr_pipe);
    }
    for (port = 1; port <= hub->ueh_port_count; ++port)
        uhub_explore_port(hub, port);
}

static void
uhub_intr(struct usb_xfer *xfer, void *private, usb_error_t status)
{
    struct usb_external_hub *hub;
    usb_error_t submit_status;

    hub = (struct usb_external_hub *)private;
    if (hub == 0 || !hub->ueh_used || hub->ueh_dying ||
        hub->ueh_intr_xfer != xfer)
        return;
    if (status == USB_STATUS_NORMAL_COMPLETION)
        (void)usb_task_schedule(&hub->ueh_task);
    else if (status == USB_STATUS_STALLED) {
        hub->ueh_clear_stall = 1;
        (void)usb_task_schedule(&hub->ueh_task);
    } else if (status == USB_STATUS_CANCELLED ||
        status == USB_STATUS_DISCONNECTED) {
        return;
    } else {
        printf("uhub%u: interrupt transfer failed: %s\n",
            hub->ueh_unit, usb_status_string(status));
    }
    submit_status = usb_submit_xfer(xfer);
    if (submit_status != USB_STATUS_IN_PROGRESS &&
        submit_status != USB_STATUS_NORMAL_COMPLETION)
        printf("uhub%u: interrupt rearm failed: %s\n",
            hub->ueh_unit, usb_status_string(submit_status));
}

static struct usb_endpoint *
uhub_interrupt_endpoint(struct usb_interface *interface)
{
    struct usb_endpoint *endpoint;
    unsigned i;

    for (i = 0; i < interface->ui_endpoint_count; ++i) {
        endpoint = interface->ui_endpoints[i];
        if (UE_GET_DIR(endpoint->ue_desc.bEndpointAddress) == UE_DIR_IN &&
            UE_GET_XFERTYPE(endpoint->ue_desc.bmAttributes) ==
            UE_INTERRUPT && endpoint->ue_desc.bInterval != 0)
            return endpoint;
    }
    return 0;
}

static struct usb_external_hub *
uhub_alloc(void)
{
    unsigned i;

    for (i = 0; i < USB_MAX_HUBS; ++i)
        if (!usb_external_hubs[i].ueh_used) {
            usb_root_hub_zero(&usb_external_hubs[i],
                sizeof(usb_external_hubs[i]));
            usb_external_hubs[i].ueh_used = 1;
            usb_external_hubs[i].ueh_unit = i;
            return &usb_external_hubs[i];
        }
    return 0;
}

static void
uhub_cleanup(struct usb_interface *interface, struct usb_external_hub *hub)
{
    unsigned port;

    if (hub == 0)
        return;
    hub->ueh_dying = 1;
    usb_task_cancel(&hub->ueh_task);
    for (port = 0; port < hub->ueh_port_count; ++port)
        if (hub->ueh_ports[port].uep_device != 0) {
            usb_device_disconnect(hub->ueh_ports[port].uep_device);
            hub->ueh_ports[port].uep_device = 0;
        }
    if (hub->ueh_intr_xfer != 0) {
        if (hub->ueh_intr_xfer->ux_active)
            (void)usb_abort_xfer(hub->ueh_intr_xfer,
                USB_STATUS_CANCELLED);
        (void)usb_free_xfer(hub->ueh_intr_xfer);
    }
    if (hub->ueh_intr_pipe != 0)
        usb_close_pipe(hub->ueh_intr_pipe);
    if (interface != 0)
        interface->ui_private = 0;
    usb_root_hub_zero(hub, sizeof(*hub));
}

static int
uhub_match(struct usb_interface *interface)
{
    if (interface == 0 || interface->ui_device == 0)
        return 0;
    if (interface->ui_desc.bInterfaceClass == UICLASS_HUB ||
        interface->ui_device->ud_desc.bDeviceClass == UDCLASS_HUB)
        return 120;
    return 0;
}

static usb_error_t
uhub_attach_interface(struct usb_interface *interface)
{
    struct usb_external_hub *hub;
    struct usb_endpoint *endpoint;
    usb_error_t status;
    size_t actlen;
    unsigned max_packet;
    unsigned power_delay;
    unsigned port;

    if (interface->ui_device->ud_depth >= USB_HUB_MAX_DEPTH)
        return USB_STATUS_UNSUPPORTED;
    endpoint = uhub_interrupt_endpoint(interface);
    if (endpoint == 0)
        return USB_STATUS_INVALID_DESCRIPTOR;
    hub = uhub_alloc();
    if (hub == 0)
        return USB_STATUS_NO_MEMORY;
    hub->ueh_interface = interface;
    hub->ueh_device = interface->ui_device;
    interface->ui_private = hub;
    usb_task_init(&hub->ueh_task, uhub_explore, hub);

    {
        usb_device_request_t request;

        uhub_make_request(&request, UT_READ_CLASS_DEVICE,
            UR_GET_DESCRIPTOR, UDESC_HUB << 8, 0,
            USB_HUB_DESCRIPTOR_SIZE);
        status = usb_control_request(hub->ueh_device, &request,
            &hub->ueh_desc, USB_HUB_DESCRIPTOR_SIZE,
            USB_ENUM_TIMEOUT_MS, &actlen);
    }
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    if (actlen < USB_HUB_DESCRIPTOR_SIZE ||
        hub->ueh_desc.bDescriptorType != UDESC_HUB ||
        hub->ueh_desc.bDescLength < USB_HUB_DESCRIPTOR_SIZE ||
        hub->ueh_desc.bNbrPorts == 0 ||
        hub->ueh_desc.bNbrPorts > USB_MAX_HUB_PORTS) {
        status = USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }
    hub->ueh_port_count = hub->ueh_desc.bNbrPorts;
    hub->ueh_status_length = (hub->ueh_port_count + 8u) / 8u;
    max_packet = UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
    if (max_packet < hub->ueh_status_length ||
        hub->ueh_status_length > sizeof(hub->ueh_status)) {
        status = USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }

    power_delay = hub->ueh_desc.bPwrOn2PwrGood * UHD_PWRON_FACTOR +
        UHUB_POWER_EXTRA_MS;
    for (port = 1; port <= hub->ueh_port_count; ++port) {
        status = uhub_port_feature(hub, port, UHF_PORT_POWER, 1);
        if (status != USB_STATUS_NORMAL_COMPLETION)
            goto fail;
        if (hub->ueh_device->ud_bus->ub_delay_ms != 0)
            hub->ueh_device->ud_bus->ub_delay_ms(
                hub->ueh_device->ud_bus->ub_delay_arg, power_delay);
    }
    status = usb_open_pipe(interface,
        endpoint->ue_desc.bEndpointAddress, &hub->ueh_intr_pipe);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    hub->ueh_intr_xfer = usb_alloc_xfer(hub->ueh_device);
    if (hub->ueh_intr_xfer == 0) {
        status = USB_STATUS_NO_MEMORY;
        goto fail;
    }
    usb_setup_xfer(hub->ueh_intr_xfer, hub->ueh_intr_pipe, hub,
        hub->ueh_status, hub->ueh_status_length, USB_XFER_SHORT_OK, 0,
        uhub_intr);
    status = usb_submit_xfer(hub->ueh_intr_xfer);
    if (status != USB_STATUS_IN_PROGRESS &&
        status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    if (usb_task_schedule(&hub->ueh_task) != 0) {
        status = USB_STATUS_NO_MEMORY;
        goto fail;
    }
    printf("uhub%u: %u ports, %s-speed hub addr=%u, powered\n",
        hub->ueh_unit, hub->ueh_port_count,
        hub->ueh_device->ud_speed == USB_SPEED_HIGH ? "high" : "full",
        hub->ueh_device->ud_address);
    return USB_STATUS_NORMAL_COMPLETION;

fail:
    uhub_cleanup(interface, hub);
    return status;
}

static void
uhub_detach_interface(struct usb_interface *interface)
{
    struct usb_external_hub *hub;
    unsigned unit;

    hub = (struct usb_external_hub *)interface->ui_private;
    if (hub == 0 || !hub->ueh_used)
        return;
    unit = hub->ueh_unit;
    uhub_cleanup(interface, hub);
    printf("uhub%u: detached\n", unit);
}

static const struct usb_driver uhub_driver = {
    "uhub",
    uhub_match,
    uhub_attach_interface,
    uhub_detach_interface
};

usb_error_t
uhub_register(struct usb_core *core)
{
    return usb_driver_register(core, &uhub_driver);
}

unsigned
usb_external_hub_count(void)
{
    unsigned count;
    unsigned i;

    count = 0;
    for (i = 0; i < USB_MAX_HUBS; ++i)
        if (usb_external_hubs[i].ueh_used)
            ++count;
    return count;
}

struct usb_device *
usb_external_hub_device(struct usb_device *hub_device, unsigned port)
{
    unsigned i;

    if (port == 0)
        return 0;
    for (i = 0; i < USB_MAX_HUBS; ++i)
        if (usb_external_hubs[i].ueh_used &&
            usb_external_hubs[i].ueh_device == hub_device &&
            port <= usb_external_hubs[i].ueh_port_count)
            return usb_external_hubs[i].ueh_ports[port - 1u].uep_device;
    return 0;
}

#ifdef KERNEL
void
uhubattach(int unit)
{
    usb_error_t status;

    (void)unit;
    usb_root_hub_zero(usb_external_hubs, sizeof(usb_external_hubs));
    status = uhub_register(usb_core_default());
    if (status == USB_STATUS_NORMAL_COMPLETION)
        printf("uhub0: external hub driver ready\n");
    else
        printf("uhub0: driver registration failed: %s\n",
            usb_status_string(status));
}
#endif
