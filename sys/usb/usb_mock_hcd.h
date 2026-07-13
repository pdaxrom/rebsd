/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _USB_USB_MOCK_HCD_H_
#define _USB_USB_MOCK_HCD_H_

#include <usb/usbvar.h>

struct usb_mock_hcd {
    struct usb_hcd um_hcd;
    uByte um_device_desc[USB_DEVICE_DESCRIPTOR_SIZE];
    uByte um_config_desc[USB_MAX_CONFIG_DESCRIPTOR_SIZE];
    size_t um_config_length;
    uByte um_child_device_desc[USB_DEVICE_DESCRIPTOR_SIZE];
    uByte um_child_config_desc[USB_MAX_CONFIG_DESCRIPTOR_SIZE];
    size_t um_child_config_length;
    uByte um_hub_desc[USB_HUB_DESCRIPTOR_SIZE];
    struct usb_xfer *um_pending_xfer;
    usb_error_t um_fail_next;
    unsigned um_connected;
    unsigned um_hub_mode;
    unsigned um_hub_port_connected;
    unsigned um_hub_port_power;
    unsigned um_hub_port_enabled;
    unsigned um_hub_port_change;
    unsigned um_hub_child_speed;
    unsigned um_port_power;
    unsigned um_port_enabled;
    unsigned um_port_change;
    unsigned um_root_intr_enabled;
    unsigned um_hold_xfers;
    unsigned um_address;
    unsigned um_configuration;
    unsigned um_child_address;
    unsigned um_start_count;
    unsigned um_stop_count;
    unsigned um_open_count;
    unsigned um_close_count;
    unsigned um_submit_count;
    unsigned um_abort_count;
    unsigned um_poll_count;
    unsigned um_clear_halt_count;
    unsigned um_clear_toggle_count;
    unsigned um_last_clear_endpoint;
};

void usb_mock_hcd_init(struct usb_mock_hcd *);
void usb_mock_hcd_set_connected(struct usb_mock_hcd *, int);
void usb_mock_hcd_fail_next(struct usb_mock_hcd *, usb_error_t);
void usb_mock_hcd_hold_xfers(struct usb_mock_hcd *, int);
usb_error_t usb_mock_hcd_set_config(struct usb_mock_hcd *,
    const void *, size_t);
void usb_mock_hcd_enable_hub(struct usb_mock_hcd *);
void usb_mock_hcd_hub_port_connect(struct usb_mock_hcd *, int, unsigned);
usb_error_t usb_mock_hcd_hub_interrupt(struct usb_mock_hcd *);

#endif /* _USB_USB_MOCK_HCD_H_ */
