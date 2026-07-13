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

#include <usb/usb_desc.h>

static void
usb_bytes_copy(void *vdst, const void *vsrc, size_t length)
{
    uByte *dst;
    const uByte *src;

    dst = (uByte *)vdst;
    src = (const uByte *)vsrc;
    while (length-- != 0)
        *dst++ = *src++;
}

static void
usb_bytes_zero(void *vdst, size_t length)
{
    uByte *dst;

    dst = (uByte *)vdst;
    while (length-- != 0)
        *dst++ = 0;
}

void
usb_desc_iter_init(struct usb_desc_iter *iter, const void *data, size_t length)
{
    if (iter == 0)
        return;
    iter->udi_data = (const uByte *)data;
    iter->udi_length = length;
    iter->udi_offset = 0;
    iter->udi_count = 0;
}

int
usb_desc_iter_next(struct usb_desc_iter *iter, const usb_descriptor_t **descp,
    size_t *offsetp)
{
    const uByte *raw;
    size_t remaining;
    size_t desc_length;

    if (iter == 0 || descp == 0 || iter->udi_data == 0)
        return USB_PARSE_INVALID;
    if (iter->udi_offset > iter->udi_length)
        return USB_PARSE_INVALID;
    remaining = iter->udi_length - iter->udi_offset;
    if (remaining == 0)
        return USB_PARSE_END;
    if (remaining < USB_DESCRIPTOR_SIZE)
        return USB_PARSE_TRUNCATED;
    if (iter->udi_count >= USB_MAX_DESCRIPTORS)
        return USB_PARSE_TOO_MANY;

    raw = iter->udi_data + iter->udi_offset;
    desc_length = raw[0];
    if (desc_length < USB_DESCRIPTOR_SIZE)
        return USB_PARSE_INVALID;
    if (desc_length > remaining)
        return USB_PARSE_TRUNCATED;

    *descp = (const usb_descriptor_t *)raw;
    if (offsetp != 0)
        *offsetp = iter->udi_offset;
    iter->udi_offset += desc_length;
    ++iter->udi_count;
    return USB_PARSE_OK;
}

int
usb_parse_device_descriptor(const void *data, size_t length,
    usb_device_descriptor_t *result)
{
    const uByte *raw;
    unsigned packet_size;

    if (data == 0 || result == 0)
        return USB_PARSE_INVALID;
    usb_bytes_zero(result, sizeof(*result));
    if (length < USB_DESCRIPTOR_SIZE)
        return USB_PARSE_TRUNCATED;
    raw = (const uByte *)data;
    if (raw[0] < USB_DEVICE_DESCRIPTOR_SIZE)
        return USB_PARSE_INVALID;
    if (raw[0] > length)
        return USB_PARSE_TRUNCATED;
    if (raw[1] != UDESC_DEVICE)
        return USB_PARSE_INVALID;

    usb_bytes_copy(result, raw, USB_DEVICE_DESCRIPTOR_SIZE);
    packet_size = result->bMaxPacketSize;
    if (packet_size != 8 && packet_size != 16 && packet_size != 32 &&
        packet_size != 64)
        return USB_PARSE_INVALID;
    if (result->bNumConfigurations == 0)
        return USB_PARSE_INVALID;
    return USB_PARSE_OK;
}

static int
usb_interface_complete(int have_interface, unsigned endpoint_count,
    unsigned expected_endpoints)
{
    if (!have_interface)
        return USB_PARSE_OK;
    if (endpoint_count != expected_endpoints)
        return USB_PARSE_INVALID;
    return USB_PARSE_OK;
}

int
usb_parse_config_descriptor(const void *data, size_t length,
    struct usb_parsed_config *result)
{
    struct usb_desc_iter iter;
    const usb_descriptor_t *desc;
    const usb_interface_descriptor_t *idesc;
    const usb_endpoint_descriptor_t *edesc;
    const uByte *raw;
    uByte interface_numbers[USB_MAX_INTERFACES];
    uByte alt_numbers[USB_MAX_INTERFACE_ALTS];
    uByte alt_values[USB_MAX_INTERFACE_ALTS];
    uByte endpoint_addresses[USB_MAX_ENDPOINTS_PER_INTERFACE];
    size_t offset;
    size_t total_length;
    unsigned unique_count;
    unsigned alt_count;
    unsigned endpoint_count;
    unsigned expected_endpoints;
    unsigned i;
    int current_output;
    int have_interface;
    int found;
    int status;

    if (data == 0 || result == 0)
        return USB_PARSE_INVALID;
    usb_bytes_zero(result, sizeof(*result));
    if (length < USB_CONFIG_DESCRIPTOR_SIZE)
        return USB_PARSE_TRUNCATED;
    raw = (const uByte *)data;
    if (raw[0] < USB_CONFIG_DESCRIPTOR_SIZE)
        return USB_PARSE_INVALID;
    if (raw[0] > length)
        return USB_PARSE_TRUNCATED;
    if (raw[1] != UDESC_CONFIG)
        return USB_PARSE_INVALID;
    total_length = (size_t)raw[2] | ((size_t)raw[3] << 8);
    if (total_length < raw[0])
        return USB_PARSE_INVALID;
    if (total_length > USB_MAX_CONFIG_DESCRIPTOR_SIZE)
        return USB_PARSE_TOO_LARGE;
    if (total_length > length)
        return USB_PARSE_TRUNCATED;
    if (raw[4] > USB_MAX_INTERFACES)
        return USB_PARSE_TOO_MANY;

    usb_bytes_copy(&result->upc_desc, raw,
        USB_CONFIG_DESCRIPTOR_SIZE);
    result->upc_total_length = total_length;
    usb_bytes_zero(interface_numbers, sizeof(interface_numbers));
    usb_bytes_zero(alt_numbers, sizeof(alt_numbers));
    usb_bytes_zero(alt_values, sizeof(alt_values));
    usb_bytes_zero(endpoint_addresses, sizeof(endpoint_addresses));
    unique_count = 0;
    alt_count = 0;
    endpoint_count = 0;
    expected_endpoints = 0;
    current_output = -1;
    have_interface = 0;

    usb_desc_iter_init(&iter, data, total_length);
    status = usb_desc_iter_next(&iter, &desc, &offset);
    if (status != USB_PARSE_OK || offset != 0)
        return status == USB_PARSE_OK ? USB_PARSE_INVALID : status;

    for (;;) {
        status = usb_desc_iter_next(&iter, &desc, &offset);
        if (status == USB_PARSE_END)
            break;
        if (status != USB_PARSE_OK)
            return status;

        if (desc->bDescriptorType == UDESC_CONFIG)
            return USB_PARSE_INVALID;

        if (desc->bDescriptorType == UDESC_INTERFACE) {
            status = usb_interface_complete(have_interface,
                endpoint_count, expected_endpoints);
            if (status != USB_PARSE_OK)
                return status;
            if (desc->bLength < USB_INTERFACE_DESCRIPTOR_SIZE)
                return USB_PARSE_INVALID;
            idesc = (const usb_interface_descriptor_t *)desc;
            if (idesc->bNumEndpoints >
                USB_MAX_ENDPOINTS_PER_INTERFACE)
                return USB_PARSE_TOO_MANY;

            for (i = 0; i < alt_count; ++i)
                if (alt_numbers[i] == idesc->bInterfaceNumber &&
                    alt_values[i] == idesc->bAlternateSetting)
                    return USB_PARSE_INVALID;
            if (alt_count >= USB_MAX_INTERFACE_ALTS)
                return USB_PARSE_TOO_MANY;
            alt_numbers[alt_count] = idesc->bInterfaceNumber;
            alt_values[alt_count] = idesc->bAlternateSetting;
            ++alt_count;

            found = 0;
            for (i = 0; i < unique_count; ++i)
                if (interface_numbers[i] == idesc->bInterfaceNumber) {
                    found = 1;
                    break;
                }
            if (!found) {
                if (unique_count >= result->upc_desc.bNumInterface ||
                    unique_count >= USB_MAX_INTERFACES)
                    return USB_PARSE_TOO_MANY;
                interface_numbers[unique_count++] =
                    idesc->bInterfaceNumber;
            }

            current_output = -1;
            if (idesc->bAlternateSetting == 0) {
                for (i = 0; i < result->upc_interface_count; ++i)
                    if (result->upc_interfaces[i].
                        upi_desc.bInterfaceNumber ==
                        idesc->bInterfaceNumber)
                        return USB_PARSE_INVALID;
                if (result->upc_interface_count >= USB_MAX_INTERFACES)
                    return USB_PARSE_TOO_MANY;
                current_output = (int)result->upc_interface_count++;
                usb_bytes_copy(&result->upc_interfaces[current_output].
                    upi_desc, idesc, USB_INTERFACE_DESCRIPTOR_SIZE);
                result->upc_interfaces[current_output].upi_offset = offset;
            }
            have_interface = 1;
            endpoint_count = 0;
            expected_endpoints = idesc->bNumEndpoints;
            usb_bytes_zero(endpoint_addresses,
                sizeof(endpoint_addresses));
            continue;
        }

        if (desc->bDescriptorType == UDESC_ENDPOINT) {
            struct usb_parsed_interface *parsed;
            struct usb_parsed_endpoint *endpoint;

            if (desc->bLength < USB_ENDPOINT_DESCRIPTOR_SIZE)
                return USB_PARSE_INVALID;
            if (!have_interface)
                return USB_PARSE_INVALID;
            if (endpoint_count >= expected_endpoints ||
                endpoint_count >= USB_MAX_ENDPOINTS_PER_INTERFACE)
                return USB_PARSE_TOO_MANY;
            edesc = (const usb_endpoint_descriptor_t *)desc;
            if (UE_GET_ADDR(edesc->bEndpointAddress) == 0 ||
                UE_GET_XFERTYPE(edesc->bmAttributes) == UE_CONTROL ||
                (UGETW(edesc->wMaxPacketSize) & 0x07ffu) == 0)
                return USB_PARSE_INVALID;
            for (i = 0; i < endpoint_count; ++i)
                if (endpoint_addresses[i] == edesc->bEndpointAddress)
                    return USB_PARSE_INVALID;
            endpoint_addresses[endpoint_count] = edesc->bEndpointAddress;
            if (current_output >= 0) {
                parsed = &result->upc_interfaces[current_output];
                endpoint = &parsed->upi_endpoints[endpoint_count];
                usb_bytes_copy(&endpoint->upe_desc, edesc,
                    USB_ENDPOINT_DESCRIPTOR_SIZE);
                endpoint->upe_offset = offset;
                ++parsed->upi_endpoint_count;
            }
            ++endpoint_count;
        }
    }

    status = usb_interface_complete(have_interface, endpoint_count,
        expected_endpoints);
    if (status != USB_PARSE_OK)
        return status;
    if (unique_count != result->upc_desc.bNumInterface ||
        result->upc_interface_count != unique_count)
        return USB_PARSE_INVALID;
    result->upc_descriptor_count = iter.udi_count;
    result->upc_alt_count = alt_count;
    return USB_PARSE_OK;
}

int
usb_parse_string_descriptor(const void *data, size_t length, size_t *units)
{
    const uByte *raw;
    size_t desc_length;

    if (data == 0 || units == 0)
        return USB_PARSE_INVALID;
    *units = 0;
    if (length < USB_DESCRIPTOR_SIZE)
        return USB_PARSE_TRUNCATED;
    raw = (const uByte *)data;
    desc_length = raw[0];
    if (raw[1] != UDESC_STRING || desc_length < USB_DESCRIPTOR_SIZE ||
        (desc_length & 1) != 0)
        return USB_PARSE_INVALID;
    if (desc_length > length)
        return USB_PARSE_TRUNCATED;
    *units = (desc_length - USB_DESCRIPTOR_SIZE) / sizeof(uWord);
    return USB_PARSE_OK;
}

const char *
usb_parse_status_string(int status)
{
    switch (status) {
    case USB_PARSE_OK:
        return "ok";
    case USB_PARSE_END:
        return "end";
    case USB_PARSE_INVALID:
        return "invalid descriptor";
    case USB_PARSE_TRUNCATED:
        return "truncated descriptor";
    case USB_PARSE_TOO_LARGE:
        return "descriptor set too large";
    case USB_PARSE_TOO_MANY:
        return "descriptor limit exceeded";
    default:
        return "unknown descriptor status";
    }
}
