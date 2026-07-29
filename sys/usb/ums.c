/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/types.h>
#include <sys/systm.h>

#include <input/mousevar.h>
#include <usb/ums.h>
#include <usb/usbhid.h>
#include <usb/usbvar.h>

#define UMS_MAX_DEVICES        MOUSE_MAX_DEVICES

struct ums_softc {
    unsigned um_used;
    unsigned um_unit;
    unsigned um_dying;
    int um_mouse_unit;
    struct usb_interface *um_interface;
    struct usb_pipe *um_pipe;
    struct usb_xfer *um_xfer;
    unsigned um_errors;
    unsigned um_rearming;
    unsigned um_report_size;
    unsigned char um_report[UMS_BOOT_REPORT_MAX_SIZE];
    char um_source_name[8];
};

static struct ums_softc ums_softc[UMS_MAX_DEVICES];

static void
ums_zero(void *vptr, size_t length)
{
    unsigned char *ptr;

    ptr = (unsigned char *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static struct ums_softc *
ums_alloc(void)
{
    unsigned i;

    for (i = 0; i < UMS_MAX_DEVICES; ++i)
        if (!ums_softc[i].um_used) {
            ums_zero(&ums_softc[i], sizeof(ums_softc[i]));
            ums_softc[i].um_used = 1;
            ums_softc[i].um_unit = i;
            ums_softc[i].um_mouse_unit = -1;
            ums_softc[i].um_source_name[0] = 'u';
            ums_softc[i].um_source_name[1] = 'm';
            ums_softc[i].um_source_name[2] = 's';
            ums_softc[i].um_source_name[3] = (char)('0' + i);
            ums_softc[i].um_source_name[4] = '\0';
            return &ums_softc[i];
        }
    return 0;
}

static int
ums_match(struct usb_interface *interface)
{
    const usb_interface_descriptor_t *desc;

    if (interface == 0)
        return 0;
    desc = &interface->ui_desc;
    if (desc->bInterfaceClass != UICLASS_HID ||
        desc->bInterfaceSubClass != UISUBCLASS_BOOT ||
        desc->bInterfaceProtocol != UIPROTO_BOOT_MOUSE)
        return 0;
    return 100;
}

static struct usb_endpoint *
ums_find_endpoint(struct usb_interface *interface)
{
    struct usb_endpoint *endpoint;
    unsigned i;
    unsigned max_packet;

    for (i = 0; i < interface->ui_endpoint_count; ++i) {
        endpoint = interface->ui_endpoints[i];
        max_packet = UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
        if (UE_GET_DIR(endpoint->ue_desc.bEndpointAddress) == UE_DIR_IN &&
            UE_GET_XFERTYPE(endpoint->ue_desc.bmAttributes) ==
            UE_INTERRUPT && max_packet >= UMS_BOOT_REPORT_MIN_SIZE &&
            endpoint->ue_desc.bInterval != 0)
            return endpoint;
    }
    return 0;
}

static void
ums_make_request(usb_device_request_t *request, unsigned char code,
    unsigned value, unsigned index)
{
    ums_zero(request, sizeof(*request));
    request->bmRequestType = UT_WRITE_CLASS_INTERFACE;
    request->bRequest = code;
    USETW(request->wValue, value);
    USETW(request->wIndex, index);
}

static void
ums_select_boot_protocol(struct usb_interface *interface)
{
    usb_device_request_t request;
    size_t actlen;

    ums_make_request(&request, UR_SET_PROTOCOL, USB_HID_PROTOCOL_BOOT,
        interface->ui_desc.bInterfaceNumber);
    (void)usb_control_request(interface->ui_device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
    ums_make_request(&request, UR_SET_IDLE, 0,
        interface->ui_desc.bInterfaceNumber);
    (void)usb_control_request(interface->ui_device, &request, 0, 0,
        USB_ENUM_TIMEOUT_MS, &actlen);
}

static void
ums_emit(void *arg, const struct mouse_event *event)
{
    struct ums_softc *sc;

    sc = (struct ums_softc *)arg;
    if (sc->um_mouse_unit >= 0)
        mouse_input((unsigned)sc->um_mouse_unit, event);
}

static void
ums_intr(struct usb_xfer *xfer, void *private, usb_error_t status)
{
    struct ums_softc *sc;
    usb_error_t submit_status;

    sc = (struct ums_softc *)private;
    if (sc == 0 || !sc->um_used || sc->um_dying || sc->um_xfer != xfer)
        return;
    if (sc->um_rearming)
        return;
    if (status != USB_STATUS_NORMAL_COMPLETION) {
        if (status == USB_STATUS_CANCELLED ||
            status == USB_STATUS_DISCONNECTED)
            return;
        if (++sc->um_errors == 1)
            printf("ums%u: interrupt transfer failed: %s\n",
                sc->um_unit, usb_status_string(status));
    } else {
        sc->um_errors = 0;
        ums_decode_boot_report(sc->um_report, xfer->ux_actlen,
            ums_emit, sc);
    }
    sc->um_rearming = 1;
    submit_status = usb_submit_xfer(xfer);
    sc->um_rearming = 0;
    if (submit_status != USB_STATUS_IN_PROGRESS &&
        submit_status != USB_STATUS_NORMAL_COMPLETION)
        printf("ums%u: interrupt rearm failed: %s\n", sc->um_unit,
            usb_status_string(submit_status));
}

static usb_error_t
ums_attach_interface(struct usb_interface *interface)
{
    struct ums_softc *sc;
    struct usb_endpoint *endpoint;
    usb_error_t status;

    endpoint = ums_find_endpoint(interface);
    if (endpoint == 0)
        return USB_STATUS_INVALID_DESCRIPTOR;
    sc = ums_alloc();
    if (sc == 0)
        return USB_STATUS_NO_MEMORY;
    sc->um_mouse_unit = mouse_attach(sc->um_source_name);
    if (sc->um_mouse_unit < 0) {
        status = USB_STATUS_NO_MEMORY;
        goto fail;
    }
    sc->um_interface = interface;
    interface->ui_private = sc;
    sc->um_report_size =
        UGETW(endpoint->ue_desc.wMaxPacketSize) & 0x07ffu;
    if (sc->um_report_size > sizeof(sc->um_report))
        sc->um_report_size = sizeof(sc->um_report);
    ums_select_boot_protocol(interface);
    status = usb_open_pipe(interface, endpoint->ue_desc.bEndpointAddress,
        &sc->um_pipe);
    if (status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    sc->um_xfer = usb_alloc_xfer(interface->ui_device);
    if (sc->um_xfer == 0) {
        status = USB_STATUS_NO_MEMORY;
        goto fail;
    }
    usb_setup_xfer(sc->um_xfer, sc->um_pipe, sc, sc->um_report,
        sc->um_report_size, USB_XFER_SHORT_OK, 0, ums_intr);
    status = usb_submit_xfer(sc->um_xfer);
    if (status != USB_STATUS_IN_PROGRESS &&
        status != USB_STATUS_NORMAL_COMPLETION)
        goto fail;
    printf("ums%u: HID boot mouse, interrupt in 0x%x, %u bytes "
        "every %u ms as mouse%d\n", sc->um_unit,
        endpoint->ue_desc.bEndpointAddress, sc->um_report_size,
        endpoint->ue_desc.bInterval, sc->um_mouse_unit);
    return USB_STATUS_NORMAL_COMPLETION;

fail:
    sc->um_dying = 1;
    if (sc->um_xfer != 0) {
        if (sc->um_xfer->ux_active)
            (void)usb_abort_xfer(sc->um_xfer, USB_STATUS_CANCELLED);
        (void)usb_free_xfer(sc->um_xfer);
    }
    if (sc->um_pipe != 0)
        usb_close_pipe(sc->um_pipe);
    if (sc->um_mouse_unit >= 0)
        mouse_detach((unsigned)sc->um_mouse_unit);
    interface->ui_private = 0;
    ums_zero(sc, sizeof(*sc));
    return status;
}

static void
ums_detach_interface(struct usb_interface *interface)
{
    struct ums_softc *sc;

    sc = (struct ums_softc *)interface->ui_private;
    interface->ui_private = 0;
    if (sc == 0 || !sc->um_used)
        return;
    sc->um_dying = 1;
    if (sc->um_xfer != 0) {
        if (sc->um_xfer->ux_active)
            (void)usb_abort_xfer(sc->um_xfer, USB_STATUS_CANCELLED);
        (void)usb_free_xfer(sc->um_xfer);
    }
    if (sc->um_pipe != 0)
        usb_close_pipe(sc->um_pipe);
    if (sc->um_mouse_unit >= 0)
        mouse_detach((unsigned)sc->um_mouse_unit);
    printf("ums%u: detached\n", sc->um_unit);
    ums_zero(sc, sizeof(*sc));
}

static const struct usb_driver ums_driver = {
    "ums",
    ums_match,
    ums_attach_interface,
    ums_detach_interface
};

usb_error_t
ums_register(struct usb_core *core)
{
    return usb_driver_register(core, &ums_driver);
}

void
umsattach(int unit)
{
    usb_error_t status;

    (void)unit;
    status = ums_register(usb_core_default());
    if (status == USB_STATUS_NORMAL_COMPLETION)
        printf("ums0: HID boot-mouse driver ready\n");
    else
        printf("ums0: driver registration failed: %s\n",
            usb_status_string(status));
}
