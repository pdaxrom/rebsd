/*
 * Host-side tests for USB protocol definitions and descriptor parsing.
 */

#include <stdio.h>
#include <string.h>
#include <dev/usb/usb_desc.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static const unsigned char device_descriptor[] = {
    18, UDESC_DEVICE, 0x00, 0x02,
    0, 0, 0, 64,
    0x34, 0x12, 0x78, 0x56, 0x00, 0x01,
    1, 2, 3, 1
};

static const unsigned char keyboard_config[] = {
    9, UDESC_CONFIG, 34, 0, 1, 1, 0, UC_BUS_POWERED, 50,
    9, UDESC_INTERFACE, 0, 0, 1,
        UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
    9, UDESC_HID, 0x11, 0x01, 0, 1, 0x22, 63, 0,
    7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
};

static int
test_protocol_layout(void)
{
    uWord word;
    uDWord dword;

    CHECK(sizeof(usb_device_request_t) == USB_DEVICE_REQUEST_SIZE);
    CHECK(sizeof(usb_device_descriptor_t) == USB_DEVICE_DESCRIPTOR_SIZE);
    CHECK(sizeof(usb_config_descriptor_t) == USB_CONFIG_DESCRIPTOR_SIZE);
    CHECK(sizeof(usb_interface_descriptor_t) ==
        USB_INTERFACE_DESCRIPTOR_SIZE);
    CHECK(sizeof(usb_endpoint_descriptor_t) ==
        USB_ENDPOINT_DESCRIPTOR_SIZE);

    USETW(word, 0xabcd);
    CHECK(word[0] == 0xcd && word[1] == 0xab);
    CHECK(UGETW(word) == 0xabcd);
    USETW2(word, 0x12, 0x34);
    CHECK(UGETW(word) == 0x1234);
    USETDW(dword, 0x89abcdefu);
    CHECK(dword[0] == 0xef && dword[1] == 0xcd &&
        dword[2] == 0xab && dword[3] == 0x89);
    CHECK(UGETDW(dword) == 0x89abcdefu);
    return 0;
}

static int
test_iterator(void)
{
    struct usb_desc_iter iter;
    const usb_descriptor_t *desc;
    size_t offset;
    unsigned char zero[] = { 0, 0 };
    unsigned char short_header[] = { 2 };
    unsigned char overrun[] = { 3, UDESC_STRING };

    usb_desc_iter_init(&iter, keyboard_config, sizeof(keyboard_config));
    CHECK(usb_desc_iter_next(&iter, &desc, &offset) == USB_PARSE_OK);
    CHECK(offset == 0 && desc->bDescriptorType == UDESC_CONFIG);
    CHECK(usb_desc_iter_next(&iter, &desc, &offset) == USB_PARSE_OK);
    CHECK(offset == 9 && desc->bDescriptorType == UDESC_INTERFACE);

    usb_desc_iter_init(&iter, zero, sizeof(zero));
    CHECK(usb_desc_iter_next(&iter, &desc, 0) == USB_PARSE_INVALID);
    usb_desc_iter_init(&iter, short_header, sizeof(short_header));
    CHECK(usb_desc_iter_next(&iter, &desc, 0) == USB_PARSE_TRUNCATED);
    usb_desc_iter_init(&iter, overrun, sizeof(overrun));
    CHECK(usb_desc_iter_next(&iter, &desc, 0) == USB_PARSE_TRUNCATED);
    return 0;
}

static int
test_device_descriptor(void)
{
    usb_device_descriptor_t parsed;
    unsigned char malformed[sizeof(device_descriptor)];

    CHECK(usb_parse_device_descriptor(device_descriptor,
        sizeof(device_descriptor), &parsed) == USB_PARSE_OK);
    CHECK(UGETW(parsed.bcdUSB) == 0x0200);
    CHECK(UGETW(parsed.idVendor) == 0x1234);
    CHECK(UGETW(parsed.idProduct) == 0x5678);
    CHECK(parsed.bMaxPacketSize == 64);

    memcpy(malformed, device_descriptor, sizeof(malformed));
    malformed[0] = 17;
    CHECK(usb_parse_device_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, device_descriptor, sizeof(malformed));
    malformed[1] = UDESC_CONFIG;
    CHECK(usb_parse_device_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, device_descriptor, sizeof(malformed));
    malformed[7] = 9;
    CHECK(usb_parse_device_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, device_descriptor, sizeof(malformed));
    malformed[17] = 0;
    CHECK(usb_parse_device_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    CHECK(usb_parse_device_descriptor(device_descriptor, 8, &parsed) ==
        USB_PARSE_TRUNCATED);
    return 0;
}

static int
test_valid_config(void)
{
    struct usb_parsed_config parsed;
    const struct usb_parsed_interface *iface;
    const struct usb_parsed_endpoint *endpoint;

    CHECK(usb_parse_config_descriptor(keyboard_config,
        sizeof(keyboard_config), &parsed) == USB_PARSE_OK);
    CHECK(parsed.upc_total_length == sizeof(keyboard_config));
    CHECK(parsed.upc_descriptor_count == 4);
    CHECK(parsed.upc_interface_count == 1);
    CHECK(parsed.upc_alt_count == 1);
    iface = &parsed.upc_interfaces[0];
    CHECK(iface->upi_offset == 9);
    CHECK(iface->upi_desc.bInterfaceClass == UICLASS_HID);
    CHECK(iface->upi_desc.bInterfaceSubClass == UISUBCLASS_BOOT);
    CHECK(iface->upi_desc.bInterfaceProtocol == UIPROTO_BOOT_KEYBOARD);
    CHECK(iface->upi_endpoint_count == 1);
    endpoint = &iface->upi_endpoints[0];
    CHECK(endpoint->upe_offset == 27);
    CHECK(endpoint->upe_desc.bEndpointAddress == 0x81);
    CHECK(UE_GET_XFERTYPE(endpoint->upe_desc.bmAttributes) == UE_INTERRUPT);
    CHECK(UGETW(endpoint->upe_desc.wMaxPacketSize) == 8);
    return 0;
}

static int
test_alternate_setting(void)
{
    static const unsigned char config[] = {
        9, UDESC_CONFIG, 41, 0, 1, 1, 0, UC_BUS_POWERED, 50,
        9, UDESC_INTERFACE, 3, 1, 1,
            UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
        7, UDESC_ENDPOINT, 0x82, UE_INTERRUPT, 8, 0, 10,
        9, UDESC_INTERFACE, 3, 0, 1,
            UICLASS_HID, UISUBCLASS_BOOT, UIPROTO_BOOT_KEYBOARD, 0,
        7, UDESC_ENDPOINT, 0x81, UE_INTERRUPT, 8, 0, 10
    };
    struct usb_parsed_config parsed;

    CHECK(usb_parse_config_descriptor(config, sizeof(config), &parsed) ==
        USB_PARSE_OK);
    CHECK(parsed.upc_interface_count == 1);
    CHECK(parsed.upc_alt_count == 2);
    CHECK(parsed.upc_interfaces[0].upi_desc.bInterfaceNumber == 3);
    CHECK(parsed.upc_interfaces[0].upi_desc.bAlternateSetting == 0);
    CHECK(parsed.upc_interfaces[0].upi_endpoints[0].
        upe_desc.bEndpointAddress == 0x81);
    return 0;
}

static int
test_malformed_lengths(void)
{
    unsigned char malformed[sizeof(keyboard_config)];
    struct usb_parsed_config parsed;
    size_t length;

    for (length = 0; length < sizeof(keyboard_config); ++length)
        CHECK(usb_parse_config_descriptor(keyboard_config, length,
            &parsed) != USB_PARSE_OK);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[9] = 0;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[9] = 8;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[2] = 35;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_TRUNCATED);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[2] = 33;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_TRUNCATED);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[2] = 8;
    malformed[3] = 0;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[2] = 1;
    malformed[3] = 4;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_TOO_LARGE);
    return 0;
}

static int
test_alternate_limit(void)
{
    unsigned char config[9 + (USB_MAX_INTERFACE_ALTS + 1) * 9];
    struct usb_parsed_config parsed;
    size_t offset;
    unsigned alt;

    memset(config, 0, sizeof(config));
    config[0] = 9;
    config[1] = UDESC_CONFIG;
    config[2] = (unsigned char)sizeof(config);
    config[3] = (unsigned char)(sizeof(config) >> 8);
    config[4] = 1;
    config[5] = 1;
    config[7] = UC_BUS_POWERED;
    offset = 9;
    for (alt = 0; alt <= USB_MAX_INTERFACE_ALTS; ++alt) {
        config[offset] = 9;
        config[offset + 1] = UDESC_INTERFACE;
        config[offset + 3] = (unsigned char)alt;
        config[offset + 5] = UICLASS_HID;
        offset += 9;
    }
    CHECK(usb_parse_config_descriptor(config, sizeof(config), &parsed) ==
        USB_PARSE_TOO_MANY);
    return 0;
}

static int
test_malformed_counts(void)
{
    unsigned char malformed[sizeof(keyboard_config)];
    struct usb_parsed_config parsed;

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[4] = USB_MAX_INTERFACES + 1;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_TOO_MANY);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[13] = USB_MAX_ENDPOINTS_PER_INTERFACE + 1;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_TOO_MANY);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[13] = 2;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);

    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[4] = 2;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    return 0;
}

static int
test_endpoint_validation(void)
{
    static const unsigned char duplicate[] = {
        9, UDESC_CONFIG, 32, 0, 1, 1, 0, UC_BUS_POWERED, 50,
        9, UDESC_INTERFACE, 0, 0, 2,
            UICLASS_MASS, UISUBCLASS_SCSI, UIPROTO_MASS_BBB, 0,
        7, UDESC_ENDPOINT, 0x81, UE_BULK, 64, 0, 0,
        7, UDESC_ENDPOINT, 0x81, UE_BULK, 64, 0, 0
    };
    unsigned char malformed[sizeof(keyboard_config)];
    struct usb_parsed_config parsed;

    CHECK(usb_parse_config_descriptor(duplicate, sizeof(duplicate),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[29] = 0x80;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[30] = UE_CONTROL;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    memcpy(malformed, keyboard_config, sizeof(malformed));
    malformed[31] = 0;
    malformed[32] = 0;
    CHECK(usb_parse_config_descriptor(malformed, sizeof(malformed),
        &parsed) == USB_PARSE_INVALID);
    return 0;
}

static int
test_descriptor_limit(void)
{
    unsigned char config[9 + USB_MAX_DESCRIPTORS * 2];
    struct usb_parsed_config parsed;
    size_t i;

    memset(config, 0, sizeof(config));
    config[0] = 9;
    config[1] = UDESC_CONFIG;
    config[2] = (unsigned char)sizeof(config);
    config[3] = (unsigned char)(sizeof(config) >> 8);
    config[5] = 1;
    config[7] = UC_BUS_POWERED;
    for (i = 9; i < sizeof(config); i += 2) {
        config[i] = 2;
        config[i + 1] = 0x30;
    }
    CHECK(usb_parse_config_descriptor(config, sizeof(config), &parsed) ==
        USB_PARSE_TOO_MANY);
    return 0;
}

static int
test_string_descriptor(void)
{
    unsigned char valid[] = { 6, UDESC_STRING, 'A', 0, 'B', 0 };
    unsigned char odd[] = { 5, UDESC_STRING, 'A', 0, 'B' };
    size_t units;

    CHECK(usb_parse_string_descriptor(valid, sizeof(valid), &units) ==
        USB_PARSE_OK);
    CHECK(units == 2);
    CHECK(usb_parse_string_descriptor(valid, 4, &units) ==
        USB_PARSE_TRUNCATED);
    CHECK(usb_parse_string_descriptor(odd, sizeof(odd), &units) ==
        USB_PARSE_INVALID);
    valid[1] = UDESC_DEVICE;
    CHECK(usb_parse_string_descriptor(valid, sizeof(valid), &units) ==
        USB_PARSE_INVALID);
    return 0;
}

int
main(void)
{
    CHECK(test_protocol_layout() == 0);
    CHECK(test_iterator() == 0);
    CHECK(test_device_descriptor() == 0);
    CHECK(test_valid_config() == 0);
    CHECK(test_alternate_setting() == 0);
    CHECK(test_malformed_lengths() == 0);
    CHECK(test_malformed_counts() == 0);
    CHECK(test_alternate_limit() == 0);
    CHECK(test_endpoint_validation() == 0);
    CHECK(test_descriptor_limit() == 0);
    CHECK(test_string_descriptor() == 0);
    CHECK(strcmp(usb_parse_status_string(USB_PARSE_TRUNCATED),
        "truncated descriptor") == 0);
    puts("usb_desc_test: all tests passed");
    return 0;
}
