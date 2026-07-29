/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _USB_UMS_H_
#define _USB_UMS_H_

#include <input/mousevar.h>
#include <usb/usbdi.h>

#define UMS_BOOT_REPORT_MIN_SIZE 3u
#define UMS_BOOT_REPORT_MAX_SIZE 8u

struct usb_core;

void ums_decode_boot_report(const unsigned char *, size_t,
    mouse_emit_t, void *);
usb_error_t ums_register(struct usb_core *);
void umsattach(int);

#endif /* _USB_UMS_H_ */
