/*	$NetBSD: usbdi.c,v 1.106 2005/02/26 23:58:20 perry Exp $	*/
/*	$NetBSD: usb_subr.c,v 1.122.2.1 2005/10/06 11:40:52 tron Exp $	*/

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

#include <usb/usbvar.h>

static void
usb_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
usb_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

const char *
usb_status_string(usb_error_t status)
{
    static const char *const names[] = {
        "normal completion", "in progress", "invalid", "no memory",
        "cancelled", "no address", "I/O error", "not configured",
        "timeout", "short transfer", "stalled", "disconnected",
        "invalid descriptor", "unsupported"
    };

    if ((unsigned)status >= sizeof(names) / sizeof(names[0]))
        return "unknown USB status";
    return names[status];
}

void
usb_core_init(struct usb_core *core)
{
    if (core != 0)
        usb_zero(core, sizeof(*core));
}

usb_error_t
usb_driver_register(struct usb_core *core, const struct usb_driver *driver)
{
    unsigned i;

    if (core == 0 || driver == 0 || driver->ud_match == 0 ||
        driver->ud_attach == 0 || driver->ud_detach == 0)
        return USB_STATUS_INVALID;
    for (i = 0; i < core->uc_driver_count; ++i)
        if (core->uc_drivers[i] == driver)
            return USB_STATUS_INVALID;
    if (core->uc_driver_count >= USB_MAX_DRIVERS)
        return USB_STATUS_NO_MEMORY;
    core->uc_drivers[core->uc_driver_count++] = driver;
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
usb_bus_start(struct usb_core *core, struct usb_bus *bus, struct usb_hcd *hcd,
    usb_delay_ms_t delay_ms, void *delay_arg)
{
    usb_error_t status;

    if (core == 0 || bus == 0 || hcd == 0 || hcd->uh_ops == 0 ||
        hcd->uh_ops->uho_start == 0 || hcd->uh_ops->uho_stop == 0 ||
        hcd->uh_ops->uho_open_pipe == 0 ||
        hcd->uh_ops->uho_close_pipe == 0 ||
        hcd->uh_ops->uho_submit_xfer == 0 ||
        hcd->uh_ops->uho_abort_xfer == 0 || hcd->uh_ops->uho_poll == 0)
        return USB_STATUS_INVALID;
    if (bus->ub_started || hcd->uh_running)
        return USB_STATUS_INVALID;
    usb_zero(bus, sizeof(*bus));
    bus->ub_core = core;
    bus->ub_hcd = hcd;
    bus->ub_delay_ms = delay_ms;
    bus->ub_delay_arg = delay_arg;
    hcd->uh_bus = bus;
    status = hcd->uh_ops->uho_start(hcd);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        hcd->uh_bus = 0;
        usb_zero(bus, sizeof(*bus));
        return status;
    }
    hcd->uh_running = 1;
    bus->ub_started = 1;
    return USB_STATUS_NORMAL_COMPLETION;
}

static struct usb_device *
usb_alloc_device(struct usb_bus *bus)
{
    struct usb_core *core;
    struct usb_device *device;
    unsigned i;

    core = bus->ub_core;
    for (i = 0; i < USB_MAX_DEVICES; ++i) {
        device = &core->uc_devices[i];
        if (!device->ud_used) {
            usb_zero(device, sizeof(*device));
            device->ud_used = 1;
            device->ud_connected = 1;
            device->ud_bus = bus;
            return device;
        }
    }
    return 0;
}

static struct usb_interface *
usb_alloc_interface(struct usb_core *core)
{
    unsigned i;

    for (i = 0; i < USB_MAX_CORE_INTERFACES; ++i)
        if (!core->uc_interfaces[i].ui_used) {
            usb_zero(&core->uc_interfaces[i],
                sizeof(core->uc_interfaces[i]));
            core->uc_interfaces[i].ui_used = 1;
            return &core->uc_interfaces[i];
        }
    return 0;
}

static struct usb_endpoint *
usb_alloc_endpoint(struct usb_core *core)
{
    unsigned i;

    for (i = 0; i < USB_MAX_CORE_ENDPOINTS; ++i)
        if (!core->uc_endpoints[i].ue_used) {
            usb_zero(&core->uc_endpoints[i],
                sizeof(core->uc_endpoints[i]));
            core->uc_endpoints[i].ue_used = 1;
            return &core->uc_endpoints[i];
        }
    return 0;
}

static struct usb_pipe *
usb_alloc_pipe(struct usb_core *core)
{
    unsigned i;

    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (!core->uc_pipes[i].up_used) {
            usb_zero(&core->uc_pipes[i], sizeof(core->uc_pipes[i]));
            core->uc_pipes[i].up_used = 1;
            return &core->uc_pipes[i];
        }
    return 0;
}

static int
usb_address_alloc(struct usb_bus *bus)
{
    unsigned address;
    unsigned byte;
    unsigned bit;

    for (address = 1; address <= USB_MAX_ADDRESS; ++address) {
        byte = address >> 3;
        bit = address & 7;
        if ((bus->ub_addresses[byte] & (1u << bit)) == 0) {
            bus->ub_addresses[byte] |= (uByte)(1u << bit);
            return (int)address;
        }
    }
    return -1;
}

static void
usb_address_free(struct usb_bus *bus, unsigned address)
{
    if (address != 0 && address <= USB_MAX_ADDRESS)
        bus->ub_addresses[address >> 3] &=
            (uByte)~(1u << (address & 7));
}

static usb_error_t
usb_setup_pipe(struct usb_device *device, struct usb_interface *interface,
    struct usb_endpoint *endpoint, struct usb_pipe **pipep)
{
    struct usb_pipe *pipe;
    usb_error_t status;

    pipe = usb_alloc_pipe(device->ud_bus->ub_core);
    if (pipe == 0)
        return USB_STATUS_NO_MEMORY;
    pipe->up_device = device;
    pipe->up_interface = interface;
    pipe->up_endpoint = endpoint;
    status = device->ud_bus->ub_hcd->uh_ops->uho_open_pipe(pipe);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        usb_zero(pipe, sizeof(*pipe));
        return status;
    }
    ++endpoint->ue_refcnt;
    pipe->up_running = 1;
    *pipep = pipe;
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
usb_open_pipe(struct usb_interface *interface, uByte endpoint_address,
    struct usb_pipe **pipep)
{
    struct usb_endpoint *endpoint;
    unsigned i;

    if (interface == 0 || !interface->ui_used || pipep == 0)
        return USB_STATUS_INVALID;
    *pipep = 0;
    endpoint = 0;
    for (i = 0; i < interface->ui_endpoint_count; ++i)
        if (interface->ui_endpoints[i]->ue_desc.bEndpointAddress ==
            endpoint_address) {
            endpoint = interface->ui_endpoints[i];
            break;
        }
    if (endpoint == 0)
        return USB_STATUS_INVALID;
    return usb_setup_pipe(interface->ui_device, interface, endpoint, pipep);
}

void
usb_close_pipe(struct usb_pipe *pipe)
{
    struct usb_core *core;
    unsigned i;

    if (pipe == 0 || !pipe->up_used)
        return;
    core = pipe->up_device->ud_bus->ub_core;
    for (i = 0; i < USB_MAX_XFERS; ++i)
        if (core->uc_xfers[i].ux_used && core->uc_xfers[i].ux_active &&
            core->uc_xfers[i].ux_pipe == pipe)
            (void)usb_abort_xfer(&core->uc_xfers[i],
                USB_STATUS_CANCELLED);
    pipe->up_device->ud_bus->ub_hcd->uh_ops->uho_close_pipe(pipe);
    if (pipe->up_endpoint->ue_refcnt != 0)
        --pipe->up_endpoint->ue_refcnt;
    usb_zero(pipe, sizeof(*pipe));
}

struct usb_xfer *
usb_alloc_xfer(struct usb_device *device)
{
    struct usb_core *core;
    unsigned i;

    if (device == 0 || !device->ud_used || !device->ud_connected)
        return 0;
    core = device->ud_bus->ub_core;
    for (i = 0; i < USB_MAX_XFERS; ++i)
        if (!core->uc_xfers[i].ux_used) {
            usb_zero(&core->uc_xfers[i], sizeof(core->uc_xfers[i]));
            core->uc_xfers[i].ux_used = 1;
            core->uc_xfers[i].ux_device = device;
            return &core->uc_xfers[i];
        }
    return 0;
}

usb_error_t
usb_free_xfer(struct usb_xfer *xfer)
{
    if (xfer == 0 || !xfer->ux_used)
        return USB_STATUS_INVALID;
    if (xfer->ux_active)
        return USB_STATUS_IN_PROGRESS;
    usb_zero(xfer, sizeof(*xfer));
    return USB_STATUS_NORMAL_COMPLETION;
}

void
usb_setup_xfer(struct usb_xfer *xfer, struct usb_pipe *pipe, void *private,
    void *buffer, size_t length, unsigned flags, unsigned timeout_ms,
    usb_callback_t callback)
{
    if (xfer == 0 || !xfer->ux_used || xfer->ux_active)
        return;
    xfer->ux_pipe = pipe;
    xfer->ux_private = private;
    xfer->ux_buffer = buffer;
    xfer->ux_length = length;
    xfer->ux_actlen = 0;
    xfer->ux_flags = flags;
    xfer->ux_timeout_ms = timeout_ms;
    xfer->ux_status = USB_STATUS_INVALID;
    xfer->ux_callback = callback;
    xfer->ux_done = 0;
    xfer->ux_is_control = 0;
    xfer->ux_hcpriv = 0;
}

void
usb_xfer_complete(struct usb_xfer *xfer, usb_error_t status, size_t actlen)
{
    usb_callback_t callback;

    if (xfer == 0 || !xfer->ux_used || !xfer->ux_active)
        return;
    if (actlen > xfer->ux_length) {
        actlen = xfer->ux_length;
        status = USB_STATUS_IO_ERROR;
    }
    if (status == USB_STATUS_NORMAL_COMPLETION &&
        actlen < xfer->ux_length &&
        (xfer->ux_flags & USB_XFER_SHORT_OK) == 0)
        status = USB_STATUS_SHORT_XFER;
    xfer->ux_actlen = actlen;
    xfer->ux_status = status;
    xfer->ux_active = 0;
    xfer->ux_done = 1;
    callback = xfer->ux_callback;
    if (callback != 0)
        callback(xfer, xfer->ux_private, status);
}

usb_error_t
usb_submit_xfer(struct usb_xfer *xfer)
{
    usb_error_t status;

    if (xfer == 0 || !xfer->ux_used || xfer->ux_active ||
        xfer->ux_pipe == 0 || !xfer->ux_pipe->up_used ||
        !xfer->ux_pipe->up_running)
        return USB_STATUS_INVALID;
    if (xfer->ux_device == 0 || !xfer->ux_device->ud_connected)
        return USB_STATUS_DISCONNECTED;
    xfer->ux_active = 1;
    xfer->ux_done = 0;
    xfer->ux_status = USB_STATUS_IN_PROGRESS;
    status = xfer->ux_device->ud_bus->ub_hcd->uh_ops->
        uho_submit_xfer(xfer);
    if (status != USB_STATUS_NORMAL_COMPLETION &&
        status != USB_STATUS_IN_PROGRESS && xfer->ux_active)
        usb_xfer_complete(xfer, status, 0);
    if (xfer->ux_done)
        return xfer->ux_status;
    return USB_STATUS_IN_PROGRESS;
}

usb_error_t
usb_abort_xfer(struct usb_xfer *xfer, usb_error_t reason)
{
    usb_error_t status;

    if (xfer == 0 || !xfer->ux_used)
        return USB_STATUS_INVALID;
    if (!xfer->ux_active)
        return xfer->ux_done ? xfer->ux_status : USB_STATUS_INVALID;
    status = xfer->ux_device->ud_bus->ub_hcd->uh_ops->uho_abort_xfer(xfer);
    if (xfer->ux_active)
        usb_xfer_complete(xfer,
            status == USB_STATUS_NORMAL_COMPLETION ? reason : status, 0);
    return xfer->ux_status;
}

static usb_error_t
usb_wait_xfer(struct usb_xfer *xfer)
{
    struct usb_bus *bus;
    unsigned timeout;
    unsigned elapsed;

    bus = xfer->ux_device->ud_bus;
    timeout = xfer->ux_timeout_ms;
    if (timeout == 0)
        timeout = USB_DEFAULT_TIMEOUT_MS;
    for (elapsed = 0; xfer->ux_active && elapsed < timeout; ++elapsed) {
        bus->ub_hcd->uh_ops->uho_poll(bus->ub_hcd);
        if (xfer->ux_active && bus->ub_delay_ms != 0)
            bus->ub_delay_ms(bus->ub_delay_arg, 1);
    }
    if (xfer->ux_active)
        return usb_abort_xfer(xfer, USB_STATUS_TIMEOUT);
    return xfer->ux_status;
}

usb_error_t
usb_control_request(struct usb_device *device,
    const usb_device_request_t *request, void *buffer, size_t length,
    unsigned timeout_ms, size_t *actlenp)
{
    struct usb_xfer *xfer;
    usb_error_t status;

    if (actlenp != 0)
        *actlenp = 0;
    if (device == 0 || request == 0 || !device->ud_connected ||
        device->ud_default_pipe == 0 || UGETW(request->wLength) != length ||
        (length != 0 && buffer == 0))
        return USB_STATUS_INVALID;
    xfer = usb_alloc_xfer(device);
    if (xfer == 0)
        return USB_STATUS_NO_MEMORY;
    usb_setup_xfer(xfer, device->ud_default_pipe, 0, buffer, length,
        USB_XFER_SYNCHRONOUS | USB_XFER_SHORT_OK, timeout_ms, 0);
    usb_copy(&xfer->ux_request, request, sizeof(xfer->ux_request));
    xfer->ux_is_control = 1;
    status = usb_submit_xfer(xfer);
    if (status == USB_STATUS_IN_PROGRESS)
        status = usb_wait_xfer(xfer);
    if (actlenp != 0)
        *actlenp = xfer->ux_actlen;
    (void)usb_free_xfer(xfer);
    return status;
}

usb_error_t
usb_bulk_transfer(struct usb_pipe *pipe, void *buffer, size_t length,
    unsigned flags, unsigned timeout_ms, size_t *actlenp)
{
    struct usb_xfer *xfer;
    usb_error_t status;

    if (actlenp != 0)
        *actlenp = 0;
    if (pipe == 0 || !pipe->up_used || !pipe->up_running ||
        pipe->up_endpoint == 0 || pipe->up_device == 0 ||
        !pipe->up_device->ud_connected ||
        UE_GET_XFERTYPE(pipe->up_endpoint->ue_desc.bmAttributes) !=
        UE_BULK || (length != 0 && buffer == 0) ||
        (flags & ~USB_XFER_SHORT_OK) != 0)
        return USB_STATUS_INVALID;
    xfer = usb_alloc_xfer(pipe->up_device);
    if (xfer == 0)
        return USB_STATUS_NO_MEMORY;
    usb_setup_xfer(xfer, pipe, 0, buffer, length,
        flags | USB_XFER_SYNCHRONOUS, timeout_ms, 0);
    status = usb_submit_xfer(xfer);
    if (status == USB_STATUS_IN_PROGRESS)
        status = usb_wait_xfer(xfer);
    if (actlenp != 0)
        *actlenp = xfer->ux_actlen;
    (void)usb_free_xfer(xfer);
    return status;
}

usb_error_t
usb_clear_endpoint_halt(struct usb_pipe *pipe)
{
    usb_device_request_t request;
    struct usb_hcd *hcd;
    usb_error_t status;

    if (pipe == 0 || !pipe->up_used || !pipe->up_running ||
        pipe->up_device == 0 || !pipe->up_device->ud_connected ||
        pipe->up_endpoint == 0)
        return USB_STATUS_INVALID;
    usb_zero(&request, sizeof(request));
    request.bmRequestType = UT_WRITE_ENDPOINT;
    request.bRequest = UR_CLEAR_FEATURE;
    USETW(request.wValue, UF_ENDPOINT_HALT);
    USETW(request.wIndex, pipe->up_endpoint->ue_desc.bEndpointAddress);
    status = usb_control_request(pipe->up_device, &request, 0, 0,
        USB_DEFAULT_TIMEOUT_MS, 0);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        return status;
    hcd = pipe->up_device->ud_bus->ub_hcd;
    if (hcd->uh_ops->uho_clear_toggle != 0)
        hcd->uh_ops->uho_clear_toggle(pipe);
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
usb_make_request(usb_device_request_t *request, uByte type, uByte code,
    unsigned value, unsigned index, unsigned length)
{
    usb_zero(request, sizeof(*request));
    request->bmRequestType = type;
    request->bRequest = code;
    USETW(request->wValue, value);
    USETW(request->wIndex, index);
    USETW(request->wLength, length);
}

static usb_error_t
usb_parse_status_to_core(int status)
{
    if (status == USB_PARSE_TOO_MANY || status == USB_PARSE_TOO_LARGE)
        return USB_STATUS_NO_MEMORY;
    return USB_STATUS_INVALID_DESCRIPTOR;
}

static usb_error_t
usb_build_interfaces(struct usb_device *device,
    const struct usb_parsed_config *parsed)
{
    struct usb_core *core;
    struct usb_interface *interface;
    struct usb_endpoint *endpoint;
    unsigned i;
    unsigned j;

    core = device->ud_bus->ub_core;
    for (i = 0; i < parsed->upc_interface_count; ++i) {
        interface = usb_alloc_interface(core);
        if (interface == 0)
            return USB_STATUS_NO_MEMORY;
        interface->ui_device = device;
        usb_copy(&interface->ui_desc,
            &parsed->upc_interfaces[i].upi_desc,
            sizeof(interface->ui_desc));
        device->ud_interfaces[device->ud_interface_count++] = interface;
        for (j = 0;
            j < parsed->upc_interfaces[i].upi_endpoint_count; ++j) {
            endpoint = usb_alloc_endpoint(core);
            if (endpoint == 0)
                return USB_STATUS_NO_MEMORY;
            endpoint->ue_interface = interface;
            usb_copy(&endpoint->ue_desc,
                &parsed->upc_interfaces[i].upi_endpoints[j].upe_desc,
                sizeof(endpoint->ue_desc));
            interface->ui_endpoints[interface->ui_endpoint_count++] =
                endpoint;
        }
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
usb_attach_interfaces(struct usb_device *device)
{
    struct usb_core *core;
    struct usb_interface *interface;
    const struct usb_driver *best;
    usb_error_t status;
    int score;
    int best_score;
    unsigned i;
    unsigned j;

    core = device->ud_bus->ub_core;
    for (i = 0; i < device->ud_interface_count; ++i) {
        interface = device->ud_interfaces[i];
        best = 0;
        best_score = 0;
        for (j = 0; j < core->uc_driver_count; ++j) {
            score = core->uc_drivers[j]->ud_match(interface);
            if (score > best_score) {
                best_score = score;
                best = core->uc_drivers[j];
            }
        }
        if (best != 0) {
            status = best->ud_attach(interface);
            if (status != USB_STATUS_NORMAL_COMPLETION)
                return status;
            interface->ui_driver = best;
        }
    }
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
usb_device_enumerate_at(struct usb_bus *bus, struct usb_device *parent_hub,
    unsigned port, unsigned speed, struct usb_device **devicep)
{
    struct usb_core *core;
    struct usb_device *device;
    usb_device_descriptor_t parsed_device;
    usb_device_request_t request;
    uByte initial[USB_MAX_IPACKET];
    uByte device_raw[USB_DEVICE_DESCRIPTOR_SIZE];
    uByte config_header[USB_CONFIG_DESCRIPTOR_SIZE];
    size_t actlen;
    size_t total_length;
    unsigned packet_size;
    int address;
    int parse_status;
    usb_error_t status;

    if (devicep != 0)
        *devicep = 0;
    if (bus == 0 || !bus->ub_started || devicep == 0 || port == 0 ||
        (speed != USB_SPEED_LOW && speed != USB_SPEED_FULL &&
        speed != USB_SPEED_HIGH))
        return USB_STATUS_INVALID;
    if (parent_hub != 0 && (!parent_hub->ud_used ||
        !parent_hub->ud_connected || parent_hub->ud_bus != bus ||
        parent_hub->ud_depth >= USB_HUB_MAX_DEPTH))
        return USB_STATUS_INVALID;
    core = bus->ub_core;
    if (core->uc_enumerating)
        return USB_STATUS_IN_PROGRESS;
    core->uc_enumerating = 1;
    device = usb_alloc_device(bus);
    if (device == 0) {
        core->uc_enumerating = 0;
        return USB_STATUS_NO_MEMORY;
    }
    device->ud_port = (uByte)port;
    device->ud_speed = (uByte)speed;
    device->ud_parent_hub = parent_hub;
    device->ud_depth = parent_hub != 0 ? parent_hub->ud_depth + 1u : 0;
    if (parent_hub != 0 && speed != USB_SPEED_HIGH) {
        if (parent_hub->ud_speed == USB_SPEED_HIGH) {
            device->ud_tt_hub_address = parent_hub->ud_address;
            device->ud_tt_port = (uByte)port;
        } else {
            device->ud_tt_hub_address = parent_hub->ud_tt_hub_address;
            device->ud_tt_port = parent_hub->ud_tt_port;
        }
    }
    usb_zero(initial, sizeof(initial));
    usb_zero(device_raw, sizeof(device_raw));
    usb_zero(config_header, sizeof(config_header));
    device->ud_default_endpoint.ue_used = 1;
    device->ud_default_endpoint.ue_desc.bLength =
        USB_ENDPOINT_DESCRIPTOR_SIZE;
    device->ud_default_endpoint.ue_desc.bDescriptorType = UDESC_ENDPOINT;
    device->ud_default_endpoint.ue_desc.bEndpointAddress = 0;
    device->ud_default_endpoint.ue_desc.bmAttributes = UE_CONTROL;
    /* USB 2.0 requires a 64-byte default control endpoint at high speed. */
    USETW(device->ud_default_endpoint.ue_desc.wMaxPacketSize,
        speed == USB_SPEED_HIGH ? USB_2_MAX_CTRL_PACKET :
        USB_MAX_IPACKET);
    status = usb_setup_pipe(device, 0, &device->ud_default_endpoint,
        &device->ud_default_pipe);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;

    usb_make_request(&request, UT_READ_DEVICE, UR_GET_DESCRIPTOR,
        UDESC_DEVICE << 8, 0, sizeof(initial));
    status = usb_control_request(device, &request, initial, sizeof(initial),
        USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    if (actlen != sizeof(initial) || initial[0] <
        USB_DEVICE_DESCRIPTOR_SIZE || initial[1] != UDESC_DEVICE) {
        status = USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }
    packet_size = initial[7];
    if ((speed == USB_SPEED_HIGH &&
        packet_size != USB_2_MAX_CTRL_PACKET) ||
        (speed != USB_SPEED_HIGH && packet_size != 8 &&
        packet_size != 16 && packet_size != 32 && packet_size != 64)) {
        status = USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }
    USETW(device->ud_default_endpoint.ue_desc.wMaxPacketSize, packet_size);

    address = usb_address_alloc(bus);
    if (address < 0) {
        status = USB_STATUS_NO_ADDRESS;
        goto fail;
    }
    usb_make_request(&request, UT_WRITE_DEVICE, UR_SET_ADDRESS,
        (unsigned)address, 0, 0);
    status = usb_control_request(device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        usb_address_free(bus, (unsigned)address);
        goto fail;
    }
    device->ud_address = (uByte)address;
    if (bus->ub_delay_ms != 0)
        bus->ub_delay_ms(bus->ub_delay_arg, USB_SET_ADDRESS_SETTLE);

    usb_make_request(&request, UT_READ_DEVICE, UR_GET_DESCRIPTOR,
        UDESC_DEVICE << 8, 0, sizeof(device_raw));
    status = usb_control_request(device, &request, device_raw,
        sizeof(device_raw), USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    parse_status = usb_parse_device_descriptor(device_raw, actlen,
        &parsed_device);
    if (parse_status != USB_PARSE_OK) {
        status = usb_parse_status_to_core(parse_status);
        goto fail;
    }
    usb_copy(&device->ud_desc, &parsed_device, sizeof(device->ud_desc));

    usb_make_request(&request, UT_READ_DEVICE, UR_GET_DESCRIPTOR,
        UDESC_CONFIG << 8, 0, sizeof(config_header));
    status = usb_control_request(device, &request, config_header,
        sizeof(config_header), USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    if (actlen != sizeof(config_header) ||
        config_header[1] != UDESC_CONFIG) {
        status = USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }
    total_length = (size_t)config_header[2] |
        ((size_t)config_header[3] << 8);
    if (total_length < USB_CONFIG_DESCRIPTOR_SIZE ||
        total_length > sizeof(device->ud_config_data)) {
        status = total_length > sizeof(device->ud_config_data) ?
            USB_STATUS_NO_MEMORY : USB_STATUS_INVALID_DESCRIPTOR;
        goto fail;
    }
    usb_make_request(&request, UT_READ_DEVICE, UR_GET_DESCRIPTOR,
        UDESC_CONFIG << 8, 0, (unsigned)total_length);
    status = usb_control_request(device, &request, device->ud_config_data,
        total_length, USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    parse_status = usb_parse_config_descriptor(device->ud_config_data,
        actlen, &core->uc_parsed_config);
    if (parse_status != USB_PARSE_OK) {
        status = usb_parse_status_to_core(parse_status);
        goto fail;
    }
    device->ud_config_length = actlen;
    status = usb_build_interfaces(device, &core->uc_parsed_config);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;

    device->ud_config = core->uc_parsed_config.
        upc_desc.bConfigurationValue;
    usb_make_request(&request, UT_WRITE_DEVICE, UR_SET_CONFIG,
        device->ud_config, 0, 0);
    status = usb_control_request(device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    status = usb_attach_interfaces(device);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    *devicep = device;
    core->uc_enumerating = 0;
    return USB_STATUS_NORMAL_COMPLETION;

fail:
    usb_device_disconnect(device);
    core->uc_enumerating = 0;
    return status;
}

usb_error_t
usb_device_enumerate(struct usb_bus *bus, unsigned port, unsigned speed,
    struct usb_device **devicep)
{
    return usb_device_enumerate_at(bus, 0, port, speed, devicep);
}

void
usb_device_disconnect(struct usb_device *device)
{
    struct usb_core *core;
    struct usb_interface *interface;
    unsigned i;

    if (device == 0 || !device->ud_used)
        return;
    core = device->ud_bus->ub_core;

    /*
     * Tear down the topology from the leaves towards the root.  Hub class
     * drivers also forget their port pointers during detach, but ownership
     * of the device tree belongs to the USB core: a disappearing parent
     * must never leave live children or host-controller pipes behind.
     */
    for (i = 0; i < USB_MAX_DEVICES; ++i)
        if (core->uc_devices[i].ud_used &&
            core->uc_devices[i].ud_parent_hub == device)
            usb_device_disconnect(&core->uc_devices[i]);
    device->ud_connected = 0;
    for (i = 0; i < USB_MAX_XFERS; ++i)
        if (core->uc_xfers[i].ux_used && core->uc_xfers[i].ux_active &&
            core->uc_xfers[i].ux_device == device)
            (void)usb_abort_xfer(&core->uc_xfers[i],
                USB_STATUS_DISCONNECTED);
    for (i = device->ud_interface_count; i != 0; --i) {
        interface = device->ud_interfaces[i - 1];
        if (interface->ui_driver != 0)
            interface->ui_driver->ud_detach(interface);
    }
    for (i = 0; i < USB_MAX_PIPES; ++i)
        if (core->uc_pipes[i].up_used &&
            core->uc_pipes[i].up_device == device)
            usb_close_pipe(&core->uc_pipes[i]);
    for (i = 0; i < USB_MAX_XFERS; ++i)
        if (core->uc_xfers[i].ux_used &&
            core->uc_xfers[i].ux_device == device)
            core->uc_xfers[i].ux_device = 0;
    for (i = 0; i < device->ud_interface_count; ++i) {
        interface = device->ud_interfaces[i];
        while (interface->ui_endpoint_count != 0) {
            --interface->ui_endpoint_count;
            usb_zero(interface->ui_endpoints[interface->ui_endpoint_count],
                sizeof(struct usb_endpoint));
        }
        usb_zero(interface, sizeof(*interface));
    }
    usb_address_free(device->ud_bus, device->ud_address);
    usb_zero(device, sizeof(*device));
}

void
usb_bus_stop(struct usb_bus *bus)
{
    struct usb_core *core;
    unsigned i;

    if (bus == 0 || !bus->ub_started)
        return;
    core = bus->ub_core;
    for (i = 0; i < USB_MAX_DEVICES; ++i)
        if (core->uc_devices[i].ud_used &&
            core->uc_devices[i].ud_bus == bus)
            usb_device_disconnect(&core->uc_devices[i]);
    bus->ub_hcd->uh_ops->uho_stop(bus->ub_hcd);
    bus->ub_hcd->uh_running = 0;
    bus->ub_hcd->uh_bus = 0;
    usb_zero(bus, sizeof(*bus));
}

struct usb_interface *
usb_device_interface(struct usb_device *device, unsigned index)
{
    if (device == 0 || !device->ud_used ||
        index >= device->ud_interface_count)
        return 0;
    return device->ud_interfaces[index];
}

struct usb_endpoint *
usb_interface_endpoint(struct usb_interface *interface, unsigned index)
{
    if (interface == 0 || !interface->ui_used ||
        index >= interface->ui_endpoint_count)
        return 0;
    return interface->ui_endpoints[index];
}
