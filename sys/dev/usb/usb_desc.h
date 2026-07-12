/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_USB_USB_DESC_H_
#define _DEV_USB_USB_DESC_H_

#include <stddef.h>
#include <dev/usb/usb.h>

#ifndef USB_MAX_CONFIG_DESCRIPTOR_SIZE
#define USB_MAX_CONFIG_DESCRIPTOR_SIZE     1024
#endif
#ifndef USB_MAX_INTERFACES
#define USB_MAX_INTERFACES                 8
#endif
#ifndef USB_MAX_ENDPOINTS_PER_INTERFACE
#define USB_MAX_ENDPOINTS_PER_INTERFACE    8
#endif
#ifndef USB_MAX_INTERFACE_ALTS
#define USB_MAX_INTERFACE_ALTS             16
#endif
#ifndef USB_MAX_DESCRIPTORS
#define USB_MAX_DESCRIPTORS                64
#endif

enum usb_parse_status {
    USB_PARSE_OK = 0,
    USB_PARSE_END = 1,
    USB_PARSE_INVALID = -1,
    USB_PARSE_TRUNCATED = -2,
    USB_PARSE_TOO_LARGE = -3,
    USB_PARSE_TOO_MANY = -4
};

struct usb_desc_iter {
    const uByte *udi_data;
    size_t udi_length;
    size_t udi_offset;
    unsigned udi_count;
};

struct usb_parsed_endpoint {
    usb_endpoint_descriptor_t upe_desc;
    size_t upe_offset;
};

struct usb_parsed_interface {
    usb_interface_descriptor_t upi_desc;
    size_t upi_offset;
    unsigned upi_endpoint_count;
    struct usb_parsed_endpoint
        upi_endpoints[USB_MAX_ENDPOINTS_PER_INTERFACE];
};

struct usb_parsed_config {
    usb_config_descriptor_t upc_desc;
    size_t upc_total_length;
    unsigned upc_descriptor_count;
    unsigned upc_interface_count;
    unsigned upc_alt_count;
    struct usb_parsed_interface upc_interfaces[USB_MAX_INTERFACES];
};

void usb_desc_iter_init(struct usb_desc_iter *, const void *, size_t);
int usb_desc_iter_next(struct usb_desc_iter *,
    const usb_descriptor_t **, size_t *);

int usb_parse_device_descriptor(const void *, size_t,
    usb_device_descriptor_t *);
int usb_parse_config_descriptor(const void *, size_t,
    struct usb_parsed_config *);
int usb_parse_string_descriptor(const void *, size_t, size_t *);
const char *usb_parse_status_string(int);

#endif /* _DEV_USB_USB_DESC_H_ */
