/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _USB_UKBD_H_
#define _USB_UKBD_H_

#include <usb/usbdi.h>

#define UKBD_BOOT_REPORT_SIZE   8u
#define UKBD_BOOT_KEY_COUNT     6u

struct usb_core;

struct ukbd_decoder {
    uByte ukd_keys[UKBD_BOOT_KEY_COUNT];
    uByte ukd_caps_lock;
};

typedef void (*ukbd_emit_t)(void *, int);

void ukbd_decoder_init(struct ukbd_decoder *);
void ukbd_decode_boot_report(struct ukbd_decoder *, const uByte *, size_t,
    ukbd_emit_t, void *);
usb_error_t ukbd_register(struct usb_core *);
void ukbdattach(int);

#endif /* _USB_UKBD_H_ */
