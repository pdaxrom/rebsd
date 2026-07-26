/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _USB_UHUB_H_
#define _USB_UHUB_H_

#include <usb/usb_task.h>
#include <usb/usbvar.h>

enum usb_root_hub_event {
    USB_ROOT_HUB_EVENT_ATTACH = 1,
    USB_ROOT_HUB_EVENT_DETACH,
    USB_ROOT_HUB_EVENT_STATUS_ERROR,
    USB_ROOT_HUB_EVENT_POWER_ERROR,
    USB_ROOT_HUB_EVENT_RESET_ERROR,
    USB_ROOT_HUB_EVENT_ENUM_ERROR
};

typedef void (*usb_root_hub_event_t)(void *, unsigned,
    enum usb_root_hub_event, struct usb_device *, usb_error_t);

struct usb_root_hub_port {
    struct usb_device *urp_device;
};

struct usb_root_hub {
    struct usb_bus *urh_bus;
    struct usb_task urh_task;
    struct usb_root_hub_port urh_ports[USB_MAX_ROOT_PORTS];
    usb_root_hub_event_t urh_event;
    void *urh_event_arg;
    usb_error_t urh_last_error;
    unsigned urh_port_count;
    unsigned urh_recover_ports;
    unsigned urh_started;
};

struct usb_external_hub_port {
    struct usb_device *uep_device;
};

struct usb_external_hub {
    unsigned ueh_used;
    unsigned ueh_unit;
    unsigned ueh_dying;
    struct usb_interface *ueh_interface;
    struct usb_device *ueh_device;
    struct usb_pipe *ueh_intr_pipe;
    struct usb_xfer *ueh_intr_xfer;
    struct usb_task ueh_task;
    usb_hub_descriptor_t ueh_desc;
    struct usb_external_hub_port ueh_ports[USB_MAX_HUB_PORTS];
    uByte ueh_status[(USB_MAX_HUB_PORTS + 8u) / 8u];
    unsigned ueh_port_count;
    unsigned ueh_status_length;
    unsigned ueh_clear_stall;
    unsigned ueh_intr_errors;
    unsigned ueh_recover;
};

usb_error_t usb_root_hub_start(struct usb_root_hub *, struct usb_bus *,
    usb_root_hub_event_t, void *);
void usb_root_hub_stop(struct usb_root_hub *);
struct usb_device *usb_root_hub_device(struct usb_root_hub *, unsigned);
usb_error_t usb_root_hub_recover_device(struct usb_device *);

usb_error_t uhub_register(struct usb_core *);
#ifdef KERNEL
void uhubattach(int);
#endif
unsigned usb_external_hub_count(void);
struct usb_device *usb_external_hub_device(struct usb_device *, unsigned);

#endif /* _USB_UHUB_H_ */
