/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DEV_USB_UHUB_H_
#define _DEV_USB_UHUB_H_

#include <dev/usb/usb_task.h>
#include <dev/usb/usbvar.h>

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
    unsigned urh_started;
};

usb_error_t usb_root_hub_start(struct usb_root_hub *, struct usb_bus *,
    usb_root_hub_event_t, void *);
void usb_root_hub_stop(struct usb_root_hub *);
struct usb_device *usb_root_hub_device(struct usb_root_hub *, unsigned);

#endif /* _DEV_USB_UHUB_H_ */
