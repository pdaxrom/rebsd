/*	$NetBSD: ukbd.c,v 1.85.16.1 2005/05/01 16:49:10 tron Exp $	*/

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

#include <sys/types.h>
#include <sys/systm.h>
#include <usb/ukbd.h>
#include <usb/usbhid.h>
#include <usb/usbvar.h>

#define UKBD_MAX_DEVICES        2u

struct ukbd_softc {
    unsigned uk_used;
    unsigned uk_unit;
    unsigned uk_dying;
    struct usb_interface *uk_interface;
    struct usb_pipe *uk_pipe;
    struct usb_xfer *uk_xfer;
    struct ukbd_decoder uk_decoder;
    uByte uk_report[UKBD_BOOT_REPORT_SIZE];
};

static struct ukbd_softc ukbd_softc[UKBD_MAX_DEVICES];

static void
ukbd_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static struct ukbd_softc *
ukbd_alloc(void)
{
    unsigned i;

    for (i = 0; i < UKBD_MAX_DEVICES; ++i)
        if (!ukbd_softc[i].uk_used) {
            ukbd_zero(&ukbd_softc[i], sizeof(ukbd_softc[i]));
            ukbd_softc[i].uk_used = 1;
            ukbd_softc[i].uk_unit = i;
            return &ukbd_softc[i];
        }
    return 0;
}

static int
ukbd_match(struct usb_interface *interface)
{
    const usb_interface_descriptor_t *desc;

    if (interface == 0)
        return 0;
    desc = &interface->ui_desc;
    if (desc->bInterfaceClass != UICLASS_HID ||
        desc->bInterfaceSubClass != UISUBCLASS_BOOT ||
        desc->bInterfaceProtocol != UIPROTO_BOOT_KEYBOARD)
        return 0;
    return 100;
}

static struct usb_endpoint *
ukbd_find_endpoint(struct usb_interface *interface)
{
    struct usb_endpoint *endpoint;
    unsigned i;
    unsigned max_packet;

    for (i = 0; i < interface->ui_endpoint_count; ++i) {
        endpoint = interface->ui_endpoints[i];
        max_packet = UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
        if (UE_GET_DIR(endpoint->ue_desc.bEndpointAddress) == UE_DIR_IN &&
            UE_GET_XFERTYPE(endpoint->ue_desc.bmAttributes) ==
            UE_INTERRUPT && max_packet >= UKBD_BOOT_REPORT_SIZE &&
            endpoint->ue_desc.bInterval != 0)
            return endpoint;
    }
    return 0;
}

static void
ukbd_make_request(usb_device_request_t *request, uByte code,
    unsigned value, unsigned index)
{
    ukbd_zero(request, sizeof(*request));
    request->bmRequestType = UT_WRITE_CLASS_INTERFACE;
    request->bRequest = code;
    USETW(request->wValue, value);
    USETW(request->wIndex, index);
}

static void
ukbd_select_boot_protocol(struct usb_interface *interface)
{
    usb_device_request_t request;
    size_t actlen;

    ukbd_make_request(&request, UR_SET_PROTOCOL, USB_HID_PROTOCOL_BOOT,
        interface->ui_desc.bInterfaceNumber);
    (void)usb_control_request(interface->ui_device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
    ukbd_make_request(&request, UR_SET_IDLE, 0,
        interface->ui_desc.bInterfaceNumber);
    (void)usb_control_request(interface->ui_device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
}

static void
ukbd_console_emit(void *arg, int character)
{
    (void)arg;
    cninput(character);
}

static void
ukbd_intr(struct usb_xfer *xfer, void *private, usb_error_t status)
{
    struct ukbd_softc *sc;
    usb_error_t submit_status;

    sc = (struct ukbd_softc *)private;
    if (sc == 0 || !sc->uk_used || sc->uk_dying || sc->uk_xfer != xfer)
        return;
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        if (status != USB_STATUS_CANCELLED &&
            status != USB_STATUS_DISCONNECTED)
            printf("ukbd%u: interrupt transfer failed: %s\n",
                sc->uk_unit, usb_status_string(status));
        return;
    }
    if (xfer->ux_actlen == UKBD_BOOT_REPORT_SIZE)
        ukbd_decode_boot_report(&sc->uk_decoder, sc->uk_report,
            xfer->ux_actlen, ukbd_console_emit, 0);
    submit_status = usb_submit_xfer(xfer);
    if (submit_status != USB_STATUS_IN_PROGRESS &&
        submit_status != USB_STATUS_NORMAL_COMPLETION)
        printf("ukbd%u: interrupt rearm failed: %s\n", sc->uk_unit,
            usb_status_string(submit_status));
}

static usb_error_t
ukbd_attach_interface(struct usb_interface *interface)
{
    struct ukbd_softc *sc;
    struct usb_endpoint *endpoint;
    usb_error_t status;

    endpoint = ukbd_find_endpoint(interface);
    if (endpoint == 0)
        return USB_STATUS_INVALID_DESCRIPTOR;
    sc = ukbd_alloc();
    if (sc == 0)
        return USB_STATUS_NO_MEMORY;
    sc->uk_interface = interface;
    interface->ui_private = sc;
    ukbd_decoder_init(&sc->uk_decoder);
    ukbd_select_boot_protocol(interface);
    status = usb_open_pipe(interface, endpoint->ue_desc.bEndpointAddress,
        &sc->uk_pipe);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    sc->uk_xfer = usb_alloc_xfer(interface->ui_device);
    if (sc->uk_xfer == 0) {
        status = USB_STATUS_NO_MEMORY;
        goto fail;
    }
    usb_setup_xfer(sc->uk_xfer, sc->uk_pipe, sc, sc->uk_report,
        sizeof(sc->uk_report), USB_XFER_SHORT_OK, 0, ukbd_intr);
    status = usb_submit_xfer(sc->uk_xfer);
    if (status != USB_STATUS_IN_PROGRESS &&
        status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    printf("ukbd%u: boot keyboard, interrupt in 0x%x, %u bytes "
        "every %u ms\n", sc->uk_unit,
        endpoint->ue_desc.bEndpointAddress, UKBD_BOOT_REPORT_SIZE,
        endpoint->ue_desc.bInterval);
    return USB_STATUS_NORMAL_COMPLETION;

fail:
    sc->uk_dying = 1;
    if (sc->uk_xfer != 0) {
        if (sc->uk_xfer->ux_active)
            (void)usb_abort_xfer(sc->uk_xfer, USB_STATUS_CANCELLED);
        (void)usb_free_xfer(sc->uk_xfer);
    }
    if (sc->uk_pipe != 0)
        usb_close_pipe(sc->uk_pipe);
    interface->ui_private = 0;
    ukbd_zero(sc, sizeof(*sc));
    return status;
}

static void
ukbd_detach_interface(struct usb_interface *interface)
{
    struct ukbd_softc *sc;

    sc = (struct ukbd_softc *)interface->ui_private;
    interface->ui_private = 0;
    if (sc == 0 || !sc->uk_used)
        return;
    sc->uk_dying = 1;
    if (sc->uk_xfer != 0) {
        if (sc->uk_xfer->ux_active)
            (void)usb_abort_xfer(sc->uk_xfer, USB_STATUS_CANCELLED);
        (void)usb_free_xfer(sc->uk_xfer);
    }
    if (sc->uk_pipe != 0)
        usb_close_pipe(sc->uk_pipe);
    printf("ukbd%u: detached\n", sc->uk_unit);
    ukbd_zero(sc, sizeof(*sc));
}

static const struct usb_driver ukbd_driver = {
    "ukbd",
    ukbd_match,
    ukbd_attach_interface,
    ukbd_detach_interface
};

usb_error_t
ukbd_register(struct usb_core *core)
{
    return usb_driver_register(core, &ukbd_driver);
}

void
ukbdattach(int unit)
{
    usb_error_t status;

    (void)unit;
    status = ukbd_register(usb_core_default());
    if (status == USB_STATUS_NORMAL_COMPLETION)
        printf("ukbd0: HID boot-keyboard driver ready\n");
    else
        printf("ukbd0: driver registration failed: %s\n",
            usb_status_string(status));
}
