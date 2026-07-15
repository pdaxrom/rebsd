/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <usb/usb_mock_hcd.h>

static const uByte mock_device_descriptor[USB_DEVICE_DESCRIPTOR_SIZE] = {
    18, UDESC_DEVICE, 0x00, 0x02, 0, 0, 0, 64,
    0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 3, 1
};

static const uByte mock_keyboard_config[] = {
    9, UDESC_CONFIG, 34, 0, 1, 1, 0, UC_BUS_POWERED, 50,
    9, UDESC_INTERFACE, 0, 0, 1,
        UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
    9, UDESC_HID, 0x11, 0x01, 0, 1, 0x22, 63, 0,
    7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
};

static const uByte mock_hub_device_descriptor[USB_DEVICE_DESCRIPTOR_SIZE] = {
    18, UDESC_DEVICE, 0x00, 0x02, UDCLASS_HUB, 0, UIPROTO_HSHUBMTT, 64,
    0x21, 0x04, 0x16, 0x25, 0x00, 0x01, 1, 2, 3, 1
};

static const uByte mock_hub_config[] = {
    9, UDESC_CONFIG, 25, 0, 1, 1, 0, UC_SELF_POWERED, 0,
    9, UDESC_INTERFACE, 0, 0, 1,
        UICLASS_HUB, UISUBCLASS_HUB, UIPROTO_HSHUBMTT, 0,
    7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 1, 0, 12
};

static const uByte mock_hub_descriptor[USB_HUB_DESCRIPTOR_SIZE] = {
    USB_HUB_DESCRIPTOR_SIZE, UDESC_HUB, 1,
    UHD_PWR_INDIVIDUAL, 0, 10, 0, 0, 0xff
};

static void
mock_zero(void *vptr, size_t length)
{
    uByte *ptr;

    ptr = (uByte *)vptr;
    while (length-- != 0)
        *ptr++ = 0;
}

static void
mock_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

static usb_error_t
mock_start(struct usb_hcd *hcd)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    ++mock->um_start_count;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
mock_stop(struct usb_hcd *hcd)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    ++mock->um_stop_count;
    mock->um_pending_xfer = 0;
    mock->um_root_intr_enabled = 0;
}

static usb_error_t
mock_open_pipe(struct usb_pipe *pipe)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)pipe->up_device->ud_bus->
        ub_hcd->uh_softc;
    ++mock->um_open_count;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
mock_close_pipe(struct usb_pipe *pipe)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)pipe->up_device->ud_bus->
        ub_hcd->uh_softc;
    ++mock->um_close_count;
}

static size_t
mock_min(size_t a, size_t b)
{
    return a < b ? a : b;
}

static usb_error_t
mock_control(struct usb_mock_hcd *mock, struct usb_xfer *xfer)
{
    const usb_device_request_t *request;
    const uByte *source;
    size_t source_length;
    size_t length;
    unsigned value;
    unsigned descriptor_type;
    unsigned child;
    unsigned index;
    unsigned flags;
    unsigned change;

    child = xfer->ux_device->ud_parent_hub != 0;

    request = &xfer->ux_request;
    value = UGETW(request->wValue);
    if (request->bRequest == UR_GET_DESCRIPTOR &&
        (request->bmRequestType & UT_READ) != 0) {
        descriptor_type = value >> 8;
        source = 0;
        source_length = 0;
        if (descriptor_type == UDESC_DEVICE) {
            source = child ? mock->um_child_device_desc :
                mock->um_device_desc;
            source_length = USB_DEVICE_DESCRIPTOR_SIZE;
        } else if (descriptor_type == UDESC_CONFIG) {
            source = child ? mock->um_child_config_desc :
                mock->um_config_desc;
            source_length = child ? mock->um_child_config_length :
                mock->um_config_length;
        } else if (descriptor_type == UDESC_HUB && mock->um_hub_mode &&
            !child && request->bmRequestType == UT_READ_CLASS_DEVICE) {
            source = mock->um_hub_desc;
            source_length = sizeof(mock->um_hub_desc);
        }
        if (source == 0) {
            usb_xfer_complete(xfer, USB_STATUS_STALLED, 0);
            return USB_STATUS_NORMAL_COMPLETION;
        }
        length = mock_min(xfer->ux_length, source_length);
        mock_copy(xfer->ux_buffer, source, length);
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, length);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (request->bRequest == UR_SET_ADDRESS &&
        request->bmRequestType == UT_WRITE_DEVICE) {
        if (child)
            mock->um_child_address = value;
        else
            mock->um_address = value;
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (request->bRequest == UR_SET_CONFIG &&
        request->bmRequestType == UT_WRITE_DEVICE) {
        mock->um_configuration = value;
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    index = UGETW(request->wIndex);
    if (mock->um_hub_mode && !child && index == 1 &&
        request->bRequest == UR_GET_STATUS &&
        request->bmRequestType == UT_READ_CLASS_OTHER &&
        xfer->ux_length == sizeof(usb_port_status_t)) {
        usb_port_status_t port_status;

        flags = 0;
        if (mock->um_hub_port_connected)
            flags |= UPS_CURRENT_CONNECT_STATUS;
        if (mock->um_hub_port_enabled)
            flags |= UPS_PORT_ENABLED;
        if (mock->um_hub_port_power)
            flags |= UPS_PORT_POWER;
        if (mock->um_hub_child_speed == USB_SPEED_LOW)
            flags |= UPS_LOW_SPEED;
        else if (mock->um_hub_child_speed == USB_SPEED_HIGH)
            flags |= UPS_HIGH_SPEED;
        USETW(port_status.wPortStatus, flags);
        USETW(port_status.wPortChange, mock->um_hub_port_change);
        mock_copy(xfer->ux_buffer, &port_status, sizeof(port_status));
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION,
            sizeof(port_status));
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (mock->um_hub_mode && !child && index == 1 &&
        request->bmRequestType == UT_WRITE_CLASS_OTHER &&
        request->bRequest == UR_SET_FEATURE) {
        if (value == UHF_PORT_POWER)
            mock->um_hub_port_power = 1;
        else if (value == UHF_PORT_RESET) {
            if (!mock->um_hub_port_connected) {
                usb_xfer_complete(xfer, USB_STATUS_DISCONNECTED, 0);
                return USB_STATUS_NORMAL_COMPLETION;
            }
            mock->um_hub_port_enabled = 1;
            mock->um_hub_port_change |= UPS_C_PORT_RESET;
        }
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (mock->um_hub_mode && !child && index == 1 &&
        request->bmRequestType == UT_WRITE_CLASS_OTHER &&
        request->bRequest == UR_CLEAR_FEATURE) {
        change = 0;
        if (value == UHF_C_PORT_CONNECTION)
            change = UPS_C_CONNECT_STATUS;
        else if (value == UHF_C_PORT_ENABLE)
            change = UPS_C_PORT_ENABLED;
        else if (value == UHF_C_PORT_SUSPEND)
            change = UPS_C_SUSPEND;
        else if (value == UHF_C_PORT_OVER_CURRENT)
            change = UPS_C_OVERCURRENT_INDICATOR;
        else if (value == UHF_C_PORT_RESET)
            change = UPS_C_PORT_RESET;
        else if (value == UHF_PORT_ENABLE)
            mock->um_hub_port_enabled = 0;
        if (value == UHF_C_PORT_CONNECTION &&
            mock->um_hub_reconnect_on_clear) {
            mock->um_hub_reconnect_on_clear = 0;
            mock->um_hub_port_connected = 1;
            mock->um_hub_port_enabled = 0;
            mock->um_hub_port_change |= UPS_C_CONNECT_STATUS;
        }
        mock->um_hub_port_change &= ~change;
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (request->bRequest == UR_CLEAR_FEATURE &&
        request->bmRequestType == UT_WRITE_ENDPOINT &&
        value == UF_ENDPOINT_HALT) {
        ++mock->um_clear_halt_count;
        mock->um_last_clear_endpoint = UGETW(request->wIndex);
        usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    usb_xfer_complete(xfer, USB_STATUS_STALLED, 0);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_submit_xfer(struct usb_xfer *xfer)
{
    struct usb_mock_hcd *mock;
    usb_error_t failure;

    mock = (struct usb_mock_hcd *)xfer->ux_device->ud_bus->
        ub_hcd->uh_softc;
    ++mock->um_submit_count;
    failure = mock->um_fail_next;
    mock->um_fail_next = USB_STATUS_NORMAL_COMPLETION;
    if (failure != USB_STATUS_NORMAL_COMPLETION) {
        usb_xfer_complete(xfer, failure, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if ((!xfer->ux_device->ud_parent_hub && !mock->um_connected) ||
        (mock->um_hub_mode && xfer->ux_device->ud_parent_hub &&
        !mock->um_hub_port_connected)) {
        usb_xfer_complete(xfer, USB_STATUS_DISCONNECTED, 0);
        return USB_STATUS_NORMAL_COMPLETION;
    }
    if (mock->um_hold_xfers) {
        if (mock->um_pending_xfer != 0)
            return USB_STATUS_NO_MEMORY;
        mock->um_pending_xfer = xfer;
        return USB_STATUS_IN_PROGRESS;
    }
    if (xfer->ux_is_control)
        return mock_control(mock, xfer);
    if (mock->um_hub_mode && xfer->ux_device->ud_parent_hub == 0) {
        if (mock->um_pending_xfer != 0)
            return USB_STATUS_NO_MEMORY;
        mock->um_pending_xfer = xfer;
        return USB_STATUS_IN_PROGRESS;
    }
    usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION, xfer->ux_length);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_abort_xfer(struct usb_xfer *xfer)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)xfer->ux_device->ud_bus->
        ub_hcd->uh_softc;
    ++mock->um_abort_count;
    if (mock->um_pending_xfer == xfer)
        mock->um_pending_xfer = 0;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_root_ctrl(struct usb_hcd *hcd, const usb_device_request_t *request,
    void *buffer, size_t *length)
{
    (void)hcd;
    (void)request;
    (void)buffer;
    (void)length;
    return USB_STATUS_UNSUPPORTED;
}

static void
mock_poll(struct usb_hcd *hcd)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    ++mock->um_poll_count;
}

static unsigned
mock_root_port_count(struct usb_hcd *hcd)
{
    (void)hcd;
    return 1;
}

static usb_error_t
mock_root_port_status(struct usb_hcd *hcd, unsigned port,
    usb_port_status_t *status)
{
    struct usb_mock_hcd *mock;
    unsigned flags;

    if (port != 1 || status == 0)
        return USB_STATUS_INVALID;
    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    flags = 0;
    if (mock->um_connected)
        flags |= UPS_CURRENT_CONNECT_STATUS;
    if (mock->um_port_enabled)
        flags |= UPS_PORT_ENABLED;
    if (mock->um_port_power)
        flags |= UPS_PORT_POWER;
    USETW(status->wPortStatus, flags);
    USETW(status->wPortChange, mock->um_port_change);
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_root_port_power(struct usb_hcd *hcd, unsigned port, int on)
{
    struct usb_mock_hcd *mock;

    if (port != 1)
        return USB_STATUS_INVALID;
    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    mock->um_port_power = on != 0;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_root_port_reset(struct usb_hcd *hcd, unsigned port)
{
    struct usb_mock_hcd *mock;

    if (port != 1)
        return USB_STATUS_INVALID;
    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    if (!mock->um_connected)
        return USB_STATUS_DISCONNECTED;
    mock->um_port_enabled = 1;
    mock->um_port_change |= UPS_C_PORT_RESET;
    return USB_STATUS_NORMAL_COMPLETION;
}

static usb_error_t
mock_root_port_clear_change(struct usb_hcd *hcd, unsigned port,
    unsigned change)
{
    struct usb_mock_hcd *mock;

    if (port != 1)
        return USB_STATUS_INVALID;
    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    mock->um_port_change &= ~change;
    return USB_STATUS_NORMAL_COMPLETION;
}

static void
mock_root_intr_enable(struct usb_hcd *hcd, int on)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)hcd->uh_softc;
    mock->um_root_intr_enabled = on != 0;
    if (on && mock->um_port_change != 0 && hcd->uh_root_change != 0)
        (void)hcd->uh_root_change(hcd->uh_root_change_arg);
}

static void
mock_clear_toggle(struct usb_pipe *pipe)
{
    struct usb_mock_hcd *mock;

    mock = (struct usb_mock_hcd *)pipe->up_device->ud_bus->
        ub_hcd->uh_softc;
    ++mock->um_clear_toggle_count;
}

static const struct usb_hcd_ops mock_ops = {
    mock_start,
    mock_stop,
    mock_open_pipe,
    mock_close_pipe,
    mock_submit_xfer,
    mock_abort_xfer,
    mock_root_ctrl,
    mock_poll,
    mock_root_port_count,
    mock_root_port_status,
    mock_root_port_power,
    mock_root_port_reset,
    mock_root_port_clear_change,
    mock_root_intr_enable,
    mock_clear_toggle
};

void
usb_mock_hcd_init(struct usb_mock_hcd *mock)
{
    mock_zero(mock, sizeof(*mock));
    mock->um_hcd.uh_ops = &mock_ops;
    mock->um_hcd.uh_softc = mock;
    mock_copy(mock->um_device_desc, mock_device_descriptor,
        sizeof(mock_device_descriptor));
    mock_copy(mock->um_config_desc, mock_keyboard_config,
        sizeof(mock_keyboard_config));
    mock->um_config_length = sizeof(mock_keyboard_config);
    mock_copy(mock->um_child_device_desc, mock_device_descriptor,
        sizeof(mock_device_descriptor));
    mock_copy(mock->um_child_config_desc, mock_keyboard_config,
        sizeof(mock_keyboard_config));
    mock->um_child_config_length = sizeof(mock_keyboard_config);
    mock_copy(mock->um_hub_desc, mock_hub_descriptor,
        sizeof(mock_hub_descriptor));
    mock->um_fail_next = USB_STATUS_NORMAL_COMPLETION;
}

void
usb_mock_hcd_set_connected(struct usb_mock_hcd *mock, int connected)
{
    unsigned new_state;

    new_state = connected != 0;
    if (mock->um_connected == new_state)
        return;
    mock->um_connected = new_state;
    mock->um_port_change |= UPS_C_CONNECT_STATUS;
    if (!new_state)
        mock->um_port_enabled = 0;
    if (mock->um_root_intr_enabled &&
        mock->um_hcd.uh_root_change != 0)
        (void)mock->um_hcd.uh_root_change(
            mock->um_hcd.uh_root_change_arg);
}

void
usb_mock_hcd_fail_next(struct usb_mock_hcd *mock, usb_error_t status)
{
    mock->um_fail_next = status;
}

void
usb_mock_hcd_hold_xfers(struct usb_mock_hcd *mock, int hold)
{
    mock->um_hold_xfers = hold != 0;
}

usb_error_t
usb_mock_hcd_set_config(struct usb_mock_hcd *mock, const void *data,
    size_t length)
{
    if (mock == 0 || data == 0 || length < USB_CONFIG_DESCRIPTOR_SIZE)
        return USB_STATUS_INVALID;
    if (length > sizeof(mock->um_config_desc))
        return USB_STATUS_NO_MEMORY;
    mock_copy(mock->um_config_desc, data, length);
    mock->um_config_length = length;
    return USB_STATUS_NORMAL_COMPLETION;
}

void
usb_mock_hcd_enable_hub(struct usb_mock_hcd *mock)
{
    if (mock == 0)
        return;
    mock->um_hub_mode = 1;
    mock_copy(mock->um_device_desc, mock_hub_device_descriptor,
        sizeof(mock_hub_device_descriptor));
    mock_copy(mock->um_config_desc, mock_hub_config,
        sizeof(mock_hub_config));
    mock->um_config_length = sizeof(mock_hub_config);
    mock->um_hub_child_speed = USB_SPEED_FULL;
}

void
usb_mock_hcd_hub_port_connect(struct usb_mock_hcd *mock, int connected,
    unsigned speed)
{
    unsigned new_state;

    if (mock == 0 || !mock->um_hub_mode)
        return;
    new_state = connected != 0;
    if (mock->um_hub_port_connected != new_state)
        mock->um_hub_port_change |= UPS_C_CONNECT_STATUS;
    mock->um_hub_port_connected = new_state;
    if (!new_state)
        mock->um_hub_port_enabled = 0;
    if (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL ||
        speed == USB_SPEED_HIGH)
        mock->um_hub_child_speed = speed;
}

void
usb_mock_hcd_hub_reconnect_on_clear(struct usb_mock_hcd *mock,
    unsigned speed)
{
    if (mock == 0 || !mock->um_hub_mode)
        return;
    mock->um_hub_reconnect_on_clear = 1;
    if (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL ||
        speed == USB_SPEED_HIGH)
        mock->um_hub_child_speed = speed;
}

usb_error_t
usb_mock_hcd_hub_interrupt(struct usb_mock_hcd *mock)
{
    struct usb_xfer *xfer;

    if (mock == 0 || !mock->um_hub_mode || mock->um_pending_xfer == 0)
        return USB_STATUS_INVALID;
    xfer = mock->um_pending_xfer;
    mock->um_pending_xfer = 0;
    if (xfer->ux_length != 0)
        ((uByte *)xfer->ux_buffer)[0] = 0x02;
    usb_xfer_complete(xfer, USB_STATUS_NORMAL_COMPLETION,
        xfer->ux_length != 0 ? 1 : 0);
    return USB_STATUS_NORMAL_COMPLETION;
}

usb_error_t
usb_mock_hcd_hub_interrupt_error(struct usb_mock_hcd *mock,
    usb_error_t status)
{
    struct usb_xfer *xfer;

    if (mock == 0 || !mock->um_hub_mode || mock->um_pending_xfer == 0 ||
        status == USB_STATUS_NORMAL_COMPLETION)
        return USB_STATUS_INVALID;
    xfer = mock->um_pending_xfer;
    mock->um_pending_xfer = 0;
    usb_xfer_complete(xfer, status, 0);
    return USB_STATUS_NORMAL_COMPLETION;
}
